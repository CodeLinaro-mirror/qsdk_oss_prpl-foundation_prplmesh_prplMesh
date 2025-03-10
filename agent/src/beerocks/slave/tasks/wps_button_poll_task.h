#ifndef _WPS_BUTTON_POLL_TASK_H_
#define _WPS_BUTTON_POLL_TASK_H_

#include "task.h"

namespace beerocks {

class BackhaulManager;

class WpsButtonPollTask : public Task {
public:
    WpsButtonPollTask(BackhaulManager &btl_ctx);
    ~WpsButtonPollTask();

    void work() override;

private:
    int fd = -1;
    BackhaulManager &m_btl_ctx;
};

} // namespace beerocks

#endif // _WPS_BUTTON_POLL_TASK_H_
