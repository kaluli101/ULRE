﻿#include"Window.h"
#include"VKInstance.h"
#include"VKPhysicalDevice.h"
#include"VKDevice.h"
#include"VKBuffer.h"
#include"VKShader.h"
#include"VKImageView.h"
#include"VKVertexInput.h"
#include"VKDescriptorSets.h"
#include"VKRenderPass.h"
#include"VKPipelineLayout.h"
#include"VKPipeline.h"
#include"VKCommandBuffer.h"
#include"VKFormat.h"
#include"VKFramebuffer.h"
#include<hgl/math/Math.h>

#include<fstream>
#ifndef WIN32
#include<unistd.h>
#endif//

using namespace hgl;
using namespace hgl::graph;

constexpr uint32_t SCREEN_WIDTH=1280;
constexpr uint32_t SCREEN_HEIGHT=720;

VkShaderModule vs=nullptr;
VkShaderModule fs=nullptr;

struct WorldConfig
{
    Matrix4f mvp;    
}world;

char *LoadFile(const char *filename,uint32_t &file_length)
{
    std::ifstream fs;

    fs.open(filename,std::ios_base::binary);

    if(!fs.is_open())
        return(nullptr);

    fs.seekg(0,std::ios_base::end);
    file_length=fs.tellg();
    char *data=new char[file_length];

    fs.seekg(0,std::ios_base::beg);
    fs.read(data,file_length);

    fs.close();
    return data;
}

bool LoadShader(vulkan::Shader *sc,const char *filename,VkShaderStageFlagBits shader_flag)
{
    uint32_t size;
    char *data=LoadFile(filename,size);

    if(!data)
        return(false);

    if(!sc->CreateShader(shader_flag,data,size))
        return(false);

    delete[] data;
    return(true);
}

vulkan::Shader *LoadShader(VkDevice device)
{
    vulkan::Shader *sc=new vulkan::Shader(device);

    if(LoadShader(sc,"FlatColor.vert.spv",VK_SHADER_STAGE_VERTEX_BIT))
    if(LoadShader(sc,"FlatColor.frag.spv",VK_SHADER_STAGE_FRAGMENT_BIT))
        return sc;

    delete sc;
    return(nullptr);
}

vulkan::Buffer *CreateUBO(vulkan::Device *dev)
{
    {
        const VkExtent2D extent=dev->GetExtent();

        world.mvp=ortho(extent.width,extent.height);
    }

    vulkan::Buffer *ubo=dev->CreateUBO(sizeof(WorldConfig));

    uint8_t *p=ubo->Map();

    if(p)
    {
        memcpy(p,&world,sizeof(WorldConfig));
        ubo->Unmap();
    }

    return ubo;
}

constexpr float vertex_data[]=
{
    SCREEN_WIDTH*0.5,   SCREEN_HEIGHT*0.25,
    SCREEN_WIDTH*0.75,  SCREEN_HEIGHT*0.75,
    SCREEN_WIDTH*0.25,  SCREEN_HEIGHT*0.75
};
constexpr float color_data[]={1,0,0,    0,1,0,      0,0,1   };

vulkan::VertexBuffer *vertex_buffer=nullptr;
vulkan::VertexBuffer *color_buffer=nullptr;

vulkan::VertexInput *CreateVertexBuffer(vulkan::Device *dev,const vulkan::Shader *shader)
{    
    vertex_buffer   =dev->CreateVBO(FMT_RG32F,  3,vertex_data);
    color_buffer    =dev->CreateVBO(FMT_RGB32F, 3,color_data);

    vulkan::VertexInput *vi=new vulkan::VertexInput(shader);

    vi->Set("Vertex",   vertex_buffer);
    vi->Set("Color",    color_buffer);

    return vi;
}

void wait_seconds(int seconds) {
#ifdef WIN32
    Sleep(seconds * 1000);
#elif defined(__ANDROID__)
    sleep(seconds);
#else
    sleep(seconds);
#endif
}

//class ExampleFramework
//{
//    Window *win=nullptr;
//    vulkan::Instance *inst=nullptr;
//    vulkan::Device *device=nullptr;
//    vulkan::Shader *shader=nullptr;
//    vulkan::Buffer *ubo_mvp=nullptr;
//    vulkan::VertexInput *vi=nullptr;
//    vulkan::PipelineCreater
//};//

int main(int,char **)
{
    #ifdef _DEBUG
    if(!vulkan::CheckStrideBytesByFormat())
        return 0xff;
    #endif//

    Window *win=CreateRenderWindow(OS_TEXT("VulkanTest"));

    win->Create(SCREEN_WIDTH,SCREEN_HEIGHT);

    vulkan::Instance *inst=vulkan::CreateInstance(U8_TEXT("VulkanTest"));

    if(!inst)
    {
        delete win;
        return(-1);
    }

    vulkan::Device *device=inst->CreateRenderDevice(win);

    if(!device)
    {
        delete inst;
        delete win;
        return(-2);
    }

    {
        const vulkan::PhysicalDevice *render_device=device->GetPhysicalDevice();

        std::cout<<"auto select physical device: "<<render_device->GetDeviceName()<<std::endl;
    }

    vulkan::Shader *shader=LoadShader(device->GetDevice());

    if(!shader)
        return -3;

    vulkan::Buffer *ubo=CreateUBO(device);

    vulkan::VertexAttributeBinding *vis_instance=shader->CreateVertexAttributeBinding();

    vulkan::VertexInput *vi=CreateVertexBuffer(device,shader);

    vulkan::PipelineCreater pc(device);

    vulkan::DescriptorSetLayoutCreater dslc(device);

    const int ubo_world_config=shader->GetUBO("world");

    dslc.BindUBO(ubo_world_config,VK_SHADER_STAGE_VERTEX_BIT);

    vulkan::DescriptorSetLayout *dsl=dslc.Create();

    dsl->UpdateUBO(ubo_world_config,*ubo);

    vulkan::PipelineLayout *pl=CreatePipelineLayout(*device,dsl);

    pc.SetDepthTest(false);
    pc.SetDepthWrite(false);
    pc.CloseCullFace();

    pc.Set(shader);
    pc.Set(vis_instance);
    pc.Set(PRIM_TRIANGLES);
    pc.Set(*pl);

    vulkan::Pipeline *pipeline=pc.Create();

    if(!pipeline)
        return(-4);

    device->AcquireNextImage();

    vulkan::CommandBuffer *cmd_buf=device->CreateCommandBuffer();

    cmd_buf->Begin(device->GetRenderPass(),device->GetFramebuffer(0));
    cmd_buf->Bind(pipeline);
    cmd_buf->Bind(pl);
    cmd_buf->Bind(vi);
    cmd_buf->Draw(3);
    cmd_buf->End();

    device->QueueSubmit(cmd_buf);
    device->Wait();
    device->QueuePresent();

    wait_seconds(1);

    delete vertex_buffer;
    delete color_buffer;

    delete pipeline;

    delete pl;
    delete dsl;

    delete vi;
    delete ubo;

    delete shader;

    delete cmd_buf;
    delete device;
    delete inst;
    delete win;

    return 0;
}
