﻿#include"VKDevice.h"
#include<hgl/type/Pair.h>
#include"VKBuffer.h"
#include"VKImageView.h"
#include"VKCommandBuffer.h"
//#include"VKDescriptorSet.h"
#include"VKRenderPass.h"
#include"VKFramebuffer.h"
#include"VKFence.h"
#include"VKSemaphore.h"
#include"VKMaterial.h"
#include"VKDescriptorSets.h"

VK_NAMESPACE_BEGIN
namespace
{
    bool CreateVulkanBuffer(VulkanBuffer &vb,const DeviceAttribute *rsa,VkBufferUsageFlags buf_usage,VkDeviceSize size,const void *data,VkSharingMode sharing_mode)
    {
        VkBufferCreateInfo buf_info={};
        buf_info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buf_info.pNext=nullptr;
        buf_info.usage=buf_usage;
        buf_info.size=size;
        buf_info.queueFamilyIndexCount=0;
        buf_info.pQueueFamilyIndices=nullptr;
        buf_info.sharingMode=sharing_mode;
        buf_info.flags=0;

        if(vkCreateBuffer(rsa->device,&buf_info,nullptr,&vb.buffer)!=VK_SUCCESS)
            return(false);

        VkMemoryRequirements mem_reqs;
        vkGetBufferMemoryRequirements(rsa->device,vb.buffer,&mem_reqs);

        VkMemoryAllocateInfo alloc_info={};
        alloc_info.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        alloc_info.pNext=nullptr;
        alloc_info.memoryTypeIndex=0;
        alloc_info.allocationSize=mem_reqs.size;

        if(rsa->CheckMemoryType(mem_reqs.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&alloc_info.memoryTypeIndex))
        {
            if(vkAllocateMemory(rsa->device,&alloc_info,nullptr,&vb.memory)==VK_SUCCESS)
            {
                if(vkBindBufferMemory(rsa->device,vb.buffer,vb.memory,0)==VK_SUCCESS)
                {
                    vb.info.buffer=vb.buffer;
                    vb.info.offset=0;
                    vb.info.range=size;

                    if(!data)
                        return(true);

                    {
                        void *dst;

                        if(vkMapMemory(rsa->device,vb.memory,0,size,0,&dst)==VK_SUCCESS)
                        {
                            memcpy(dst,data,size);
                            vkUnmapMemory(rsa->device,vb.memory);
                            return(true);
                        }
                    }
                }

                vkFreeMemory(rsa->device,vb.memory,nullptr);
            }
        }

        vkDestroyBuffer(rsa->device,vb.buffer,nullptr);
        return(false);
    }
}//namespace

Device::Device(DeviceAttribute *da)
{
    attr=da;
    
    current_frame=0;

    image_acquired_semaphore=this->CreateSem();
    draw_fence=this->CreateFence();

    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.pNext = nullptr;
    present.swapchainCount = 1;
    present.pSwapchains = &attr->swap_chain;
    present.pWaitSemaphores = nullptr;
    present.waitSemaphoreCount = 0;
    present.pResults = nullptr;

    {
        const int sc_count=attr->sc_image_views.GetCount();

        main_rp=CreateRenderPass(attr->sc_image_views[0]->GetFormat(),attr->depth.view->GetFormat());

        for(int i=0;i<sc_count;i++)
            main_fb.Add(vulkan::CreateFramebuffer(this,main_rp,attr->sc_image_views[i],attr->depth.view));
    }
}
Device::~Device()
{
    main_fb.Clear();

    delete main_rp;

    delete image_acquired_semaphore;
    delete draw_fence;

    delete attr;
}

VertexBuffer *Device::CreateVBO(VkFormat format,uint32_t count,const void *data,VkSharingMode sharing_mode)
{
    const uint32_t stride=GetStrideByFormat(format);

    if(stride==0)
    {
        std::cerr<<"format["<<format<<"] stride length is 0,please use \"Device::CreateBuffer(VkBufferUsageFlags,VkDeviceSize,VkSharingMode)\" function.";
        return(nullptr);
    }

    const VkDeviceSize size=stride*count;

    VulkanBuffer vb;

    if(!CreateVulkanBuffer(vb,attr,VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,size,data,sharing_mode))
        return(nullptr);

    return(new VertexBuffer(attr->device,vb,format,stride,count));
}

IndexBuffer *Device::CreateIBO(VkIndexType index_type,uint32_t count,const void *data,VkSharingMode sharing_mode)
{
    uint32_t stride;
    
    if(index_type==VK_INDEX_TYPE_UINT16)stride=2;else
    if(index_type==VK_INDEX_TYPE_UINT32)stride=4;else
        return(nullptr);

    const VkDeviceSize size=stride*count;

    VulkanBuffer vb;

    if(!CreateVulkanBuffer(vb,attr,VK_BUFFER_USAGE_INDEX_BUFFER_BIT,size,data,sharing_mode))
        return(nullptr);

    return(new IndexBuffer(attr->device,vb,index_type,count));
}

Buffer *Device::CreateBuffer(VkBufferUsageFlags buf_usage,VkDeviceSize size,const void *data,VkSharingMode sharing_mode)
{
    VulkanBuffer vb;

    if(!CreateVulkanBuffer(vb,attr,buf_usage,size,data,sharing_mode))
        return(nullptr);

    return(new Buffer(attr->device,vb));
}

CommandBuffer *Device::CreateCommandBuffer()
{
    if(!attr->cmd_pool)
        return(nullptr);

    VkCommandBufferAllocateInfo cmd={};
    cmd.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    cmd.pNext=nullptr;
    cmd.commandPool=attr->cmd_pool;
    cmd.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    cmd.commandBufferCount=1;

    VkCommandBuffer cmd_buf;

    VkResult res=vkAllocateCommandBuffers(attr->device,&cmd,&cmd_buf);

    if(res!=VK_SUCCESS)
        return(nullptr);

    return(new CommandBuffer(attr->device,attr->swapchain_extent,attr->cmd_pool,cmd_buf));
}

RenderPass *Device::CreateRenderPass(VkFormat color_format,VkFormat depth_format)
{
    VkAttachmentDescription attachments[2];

    VkAttachmentReference color_reference={0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth_reference={1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

    VkSubpassDescription subpass={};
    subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.flags=0;
    subpass.inputAttachmentCount=0;
    subpass.pInputAttachments=nullptr;    
    subpass.pResolveAttachments=nullptr;
    subpass.preserveAttachmentCount=0;
    subpass.pPreserveAttachments=nullptr;
    
    int att_count=0;

    if(color_format!=VK_FORMAT_UNDEFINED)
    {
        attachments[0].format=color_format;
        attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;
        attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;
        attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[0].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[0].finalLayout=VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
        attachments[0].flags=0;

        ++att_count;

        subpass.colorAttachmentCount=1;
        subpass.pColorAttachments=&color_reference;
    }
    else
    {
        subpass.colorAttachmentCount=0;
        subpass.pColorAttachments=nullptr;
    }

    if(depth_format!=VK_FORMAT_UNDEFINED)
    {
        attachments[att_count].format=depth_format;
        attachments[att_count].samples=VK_SAMPLE_COUNT_1_BIT;
        attachments[att_count].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[att_count].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[att_count].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[att_count].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[att_count].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[att_count].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        attachments[att_count].flags=0;

        depth_reference.attachment=att_count;

        ++att_count;

        subpass.pDepthStencilAttachment=&depth_reference;
    }
    else
    {
        subpass.pDepthStencilAttachment=nullptr;
    }

    VkSubpassDependency dependency = {};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.srcAccessMask = 0;
    dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp_info={};
    rp_info.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp_info.pNext=nullptr;
    rp_info.attachmentCount=att_count;
    rp_info.pAttachments=attachments;
    rp_info.subpassCount=1;
    rp_info.pSubpasses=&subpass;
    rp_info.dependencyCount=1;
    rp_info.pDependencies=&dependency;

    VkRenderPass render_pass;

    if(vkCreateRenderPass(attr->device,&rp_info,nullptr,&render_pass)!=VK_SUCCESS)
        return(nullptr);

    return(new RenderPass(attr->device,render_pass,color_format,depth_format));
}

Fence *Device::CreateFence()
{
    VkFenceCreateInfo fenceInfo;
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.pNext = nullptr;
    fenceInfo.flags = 0;

    VkFence fence;

    if(vkCreateFence(attr->device, &fenceInfo, nullptr, &fence)!=VK_SUCCESS)
        return(nullptr);

    return(new Fence(attr->device,fence));
}

Semaphore *Device::CreateSem()
{
    VkSemaphoreCreateInfo SemaphoreCreateInfo;
    SemaphoreCreateInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    SemaphoreCreateInfo.pNext = nullptr;
    SemaphoreCreateInfo.flags = 0;

    VkSemaphore sem;
    if(vkCreateSemaphore(attr->device, &SemaphoreCreateInfo, nullptr, &sem)!=VK_SUCCESS)
        return(nullptr);

    return(new Semaphore(attr->device,sem));
}

bool Device::AcquireNextImage()
{
    return(vkAcquireNextImageKHR(attr->device,attr->swap_chain,UINT64_MAX,*image_acquired_semaphore,VK_NULL_HANDLE,&current_frame)==VK_SUCCESS);
}

bool Device::QueueSubmit(CommandBuffer *buf)
{
    if(!buf)
        return(false);

    VkPipelineStageFlags pipe_stage_flags = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit_info = {};

    VkSemaphore wait_sem=*image_acquired_semaphore;
    VkCommandBuffer cmd_bufs=*buf;

    submit_info.pNext = nullptr;
    submit_info.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit_info.waitSemaphoreCount = 1;
    submit_info.pWaitSemaphores = &wait_sem;
    submit_info.pWaitDstStageMask = &pipe_stage_flags;
    submit_info.commandBufferCount = 1;
    submit_info.pCommandBuffers = &cmd_bufs;
    submit_info.signalSemaphoreCount = 0;
    submit_info.pSignalSemaphores = nullptr;

    return(vkQueueSubmit(attr->graphics_queue, 1, &submit_info, *draw_fence)==VK_SUCCESS);
}

bool Device::Wait(bool wait_all,uint64_t time_out)
{
    VkFence fence=*draw_fence;

    vkWaitForFences(attr->device, 1, &fence, wait_all, time_out);
    vkResetFences(attr->device,1,&fence);

    return(true);
}

bool Device::QueuePresent()
{
    present.pImageIndices = &current_frame;
    present.waitSemaphoreCount = 0;
    present.pWaitSemaphores = nullptr;

    return(vkQueuePresentKHR(attr->present_queue, &present)==VK_SUCCESS);
}

Material *Device::CreateMaterial(Shader *shader)
{
    DescriptorSetLayoutCreater *dslc=new DescriptorSetLayoutCreater(this);

    return(new Material(shader,dslc));
}
VK_NAMESPACE_END
