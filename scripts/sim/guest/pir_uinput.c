// SPDX-License-Identifier: MIT
/* @canonical simulated_pir_device -- real EV_SW/SW_FRONT_PROXIMITY via uinput.
 * Root-only local datagrams accept exactly "0" or "1". Never emits touch events.
 * Initial state is explicitly present for the interactive simulation.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/uinput.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/un.h>
#include <unistd.h>
static volatile sig_atomic_t stopped;
static void stop_handler(int signal_number) { (void)signal_number; stopped=1; }
static int emit_switch(int fd, int value)
{
    struct input_event events[2] = {{.type=EV_SW,.code=SW_FRONT_PROXIMITY,.value=value},
                                    {.type=EV_SYN,.code=SYN_REPORT,.value=0}};
    return write(fd,events,sizeof(events)) == (ssize_t)sizeof(events) ? 0 : -1;
}
int main(void)
{
    const char *path="/run/bhp-sim/pir.sock";
    int input=-1,server=-1,result=1,bound=0;
    umask(0077);
    input=open("/dev/uinput",O_WRONLY|O_NONBLOCK|O_CLOEXEC);
    if(input<0 || ioctl(input,UI_SET_EVBIT,EV_SW)<0 ||
       ioctl(input,UI_SET_SWBIT,SW_FRONT_PROXIMITY)<0) goto cleanup;
    struct uinput_setup setup={.id={.bustype=BUS_VIRTUAL,.vendor=0x1234,.product=0x0001,.version=1}};
    snprintf(setup.name,sizeof(setup.name),"bhp-sim-pir");
    if(ioctl(input,UI_DEV_SETUP,&setup)<0 || ioctl(input,UI_DEV_CREATE)<0) goto cleanup;
    server=socket(AF_UNIX,SOCK_DGRAM|SOCK_CLOEXEC,0);
    struct sockaddr_un address={.sun_family=AF_UNIX};
    snprintf(address.sun_path,sizeof(address.sun_path),"%s",path);
    if(server<0 || bind(server,(struct sockaddr*)&address,sizeof(address))<0) goto cleanup;
    bound=1;
    struct sigaction action={.sa_handler=stop_handler};
    sigaction(SIGTERM,&action,NULL); sigaction(SIGINT,&action,NULL);
    if(emit_switch(input,1)<0) goto cleanup;
    while(!stopped) {
        char value[4];
        ssize_t count=recv(server,value,sizeof(value),0);
        if(count<0) {if(errno==EINTR) continue; goto cleanup;}
        if(count!=1 || (value[0]!='0' && value[0]!='1')) continue;
        if(emit_switch(input,value[0]-'0')<0) goto cleanup;
    }
    result=0;
cleanup:
    if(result) fprintf(stderr,"{\"error_code\":\"SIM_PIR.IO_FAILED\",\"evidence\":\"simulation_not_hardware_evidence\"}\n");
    if(server>=0) close(server);
    if(bound) unlink(path);
    if(input>=0) {ioctl(input,UI_DEV_DESTROY); close(input);}
    return result;
}
