#include "wps_button_poll_task.h"
#include "../backhaul_manager/backhaul_manager.h"
#include <easylogging++.h>
#include <fcntl.h>
#include <linux/input.h>
#include <unistd.h>

#define EVENT_DEVICE "/dev/input/event0"

using namespace beerocks;

WpsButtonPollTask::WpsButtonPollTask(BackhaulManager &btl_ctx)
    : Task(eTaskType::WPS_BUTTON_POLL), m_btl_ctx(btl_ctx)
{
    fd = open(EVENT_DEVICE, O_RDONLY | O_NONBLOCK);
    if (fd < 0) {
        LOG(ERROR) << "Failed to open " << EVENT_DEVICE << " errno=" << strerror(errno);
        return;
    }
    LOG(DEBUG) << "d_stolbov opened a fd";
}

WpsButtonPollTask::~WpsButtonPollTask()
{
    if (fd >= 0) {
        LOG(DEBUG) << "d_stolbov Closing fd";
        close(fd);
    }
}

void WpsButtonPollTask::work()
{
    struct input_event ev;
    //auto now = std::chrono::steady_clock::now();
    LOG(TRACE) << "d_stolbov Trying to read";
    ssize_t bytes = read(fd, &ev, sizeof(ev));
    LOG(TRACE) << "d_stolbov After read";
    if (bytes < 0) {
        LOG(ERROR) << "Failed to read from device, error=" << strerror(errno);
        return;
    }
    LOG(TRACE) << "d_stolbov Read nicely";
    if (ev.type == EV_KEY) {
        LOG(DEBUG) << "WPS button pressed";
        // do the actual logic
        LOG(DEBUG) << "d_stolbov ev.type=" << ev.type << " ev.code=" << ev.code
                   << " ev.value=" << ev.value << " ev.time=" << ev.time.tv_sec;
    }
    LOG(TRACE) << "d_stolbov ev.type=" << ev.type << " ev.code=" << ev.code
               << " ev.value=" << ev.value << " ev.time=" << ev.time.tv_sec;
}
