#include "panel_ui/drm_page_flip_display.hpp"
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <drm_fourcc.h>
#include <drm_mode.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <fcntl.h>
#include <poll.h>
#include <unistd.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstring>
#include <stdexcept>

namespace house_panel::panel_ui {
struct DrmPageFlipDisplay::Implementation {
 struct Buffer{std::uint32_t handle{0},framebuffer{0},pitch{0};std::uint64_t size{0};void *map{MAP_FAILED};};
 int fd{-1};std::uint32_t connector{0},crtc{0};drmModeModeInfo mode{};std::array<Buffer,2> buffers{};int active{0};bool complete{false};bool faulted{false};
 ~Implementation(){if(fd>=0){for(auto &b:buffers){if(b.map!=MAP_FAILED)munmap(b.map,b.size);if(b.framebuffer)drmModeRmFB(fd,b.framebuffer);if(b.handle){drm_mode_destroy_dumb d{};d.handle=b.handle;ioctl(fd,DRM_IOCTL_MODE_DESTROY_DUMB,&d);}}close(fd);}}
 static void flipped(int,unsigned int,unsigned int,unsigned int,void *data){static_cast<Implementation*>(data)->complete=true;}
 void allocate(Buffer &b){drm_mode_create_dumb c{};c.width=1024;c.height=600;c.bpp=32;if(ioctl(fd,DRM_IOCTL_MODE_CREATE_DUMB,&c)<0)throw std::runtime_error("PANEL_UI.DRM_BUFFER_CREATE_FAILED");b.handle=c.handle;b.pitch=c.pitch;b.size=c.size;std::uint32_t handles[4]{b.handle},pitches[4]{b.pitch},offsets[4]{};if(drmModeAddFB2(fd,1024,600,DRM_FORMAT_XRGB8888,handles,pitches,offsets,&b.framebuffer,0)<0)throw std::runtime_error("PANEL_UI.DRM_FRAMEBUFFER_CREATE_FAILED");drm_mode_map_dumb m{};m.handle=b.handle;if(ioctl(fd,DRM_IOCTL_MODE_MAP_DUMB,&m)<0)throw std::runtime_error("PANEL_UI.DRM_MAP_FAILED");b.map=mmap(nullptr,b.size,PROT_READ|PROT_WRITE,MAP_SHARED,fd,m.offset);if(b.map==MAP_FAILED)throw std::runtime_error("PANEL_UI.DRM_MAP_FAILED");std::memset(b.map,0,b.size);}
};
DrmPageFlipDisplay::DrmPageFlipDisplay(const std::string &card,std::uint32_t connector):implementation_(std::make_unique<Implementation>()) {
 auto &s=*implementation_;s.connector=connector;s.fd=open(card.c_str(),O_RDWR|O_CLOEXEC);if(s.fd<0)throw std::runtime_error("PANEL_UI.DRM_OPEN_FAILED");
 drmModeConnector *c=drmModeGetConnector(s.fd,connector);if(!c)throw std::runtime_error("PANEL_UI.DRM_CONNECTOR_INVALID");
 bool found=false;for(int i=0;i<c->count_modes;++i)if(c->modes[i].hdisplay==1024&&c->modes[i].vdisplay==600){s.mode=c->modes[i];found=true;break;}
 auto *resources=drmModeGetResources(s.fd);if(!resources){drmModeFreeConnector(c);throw std::runtime_error("PANEL_UI.DRM_RESOURCES_FAILED");}
 for(int i=0;i<c->count_encoders&&!s.crtc;++i){auto *e=drmModeGetEncoder(s.fd,c->encoders[i]);if(e){if(e->crtc_id)s.crtc=e->crtc_id;else for(int n=0;n<resources->count_crtcs;++n)if(e->possible_crtcs&(1u<<n)){s.crtc=resources->crtcs[n];break;}drmModeFreeEncoder(e);}}
 const bool connected=c->connection==DRM_MODE_CONNECTED;drmModeFreeConnector(c);drmModeFreeResources(resources);
 if(!found||!connected||!s.crtc)throw std::runtime_error("PANEL_UI.DRM_EXACT_MODE_UNAVAILABLE");
 s.allocate(s.buffers[0]);s.allocate(s.buffers[1]);
 if(drmModeSetCrtc(s.fd,s.crtc,s.buffers[0].framebuffer,0,0,&s.connector,1,&s.mode)!=0)throw std::runtime_error("PANEL_UI.DRM_MODESET_FAILED");
}
DrmPageFlipDisplay::~DrmPageFlipDisplay()=default;
bool DrmPageFlipDisplay::commit(const std::vector<std::uint32_t> &pixels){auto &s=*implementation_;if(s.faulted||pixels.size()!=1024u*600u)return false;int next=1-s.active;auto &b=s.buffers[next];for(int y=0;y<600;++y)std::memcpy(static_cast<char*>(b.map)+y*b.pitch,pixels.data()+y*1024,1024*4);s.complete=false;if(drmModePageFlip(s.fd,s.crtc,b.framebuffer,DRM_MODE_PAGE_FLIP_EVENT,&s)!=0){s.faulted=true;return false;}drmEventContext context{};context.version=DRM_EVENT_CONTEXT_VERSION;context.page_flip_handler=Implementation::flipped;const auto deadline=std::chrono::steady_clock::now()+std::chrono::milliseconds(500);while(!s.complete){auto remaining=std::chrono::duration_cast<std::chrono::milliseconds>(deadline-std::chrono::steady_clock::now()).count();if(remaining<=0){s.faulted=true;return false;}pollfd descriptor{s.fd,POLLIN,0};int ready=poll(&descriptor,1,static_cast<int>(remaining));if(ready<0&&errno==EINTR)continue;if(ready<=0||!(descriptor.revents&POLLIN)||drmHandleEvent(s.fd,&context)!=0){s.faulted=true;return false;}}s.active=next;return true;}
}
