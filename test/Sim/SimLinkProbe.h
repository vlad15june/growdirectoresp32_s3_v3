#pragma once

#include "Core/Domain/Interfaces/Adapters/ILinkProbe.h"
#include "Core/Domain/Interfaces/Adapters/IWifiAdapter.h"
#include "Core/Domain/Interfaces/Scheduling/ITaskScheduler.h"

/**
 * @brief The network beyond the radio, as the probe of the link meets it: a router that answers ARP
 *        or does not, and an internet that echoes or does not.
 *
 * It fails where the board's probe fails. With the station not associated there is no address to
 * ask from, and the board's probe says so without sending anything (LwipLinkProbe asks the station's
 * netif for its address and gateway first); a router that does not answer ARP forwards no echo
 * either. An answer costs the asking task a few milliseconds, silence costs it the whole wait - on
 * the board the network task blocks for both, and so does it here.
 */
class SimLinkProbe : public ILinkProbe
{
public:
    SimLinkProbe(IWifiAdapter& wifi, ITaskScheduler& scheduler) : _wifi(wifi), _scheduler(scheduler) {}

    bool routerAnswers = true;
    bool internetAnswers = true;

    /** How many times each was asked, so a test can tell a decision taken without asking. */
    int gatewayAsks = 0;
    int echoes = 0;

    LinkReach askGateway(uint32_t waitMs) override
    {
        gatewayAsks++;
        if (!_wifi.isConnected())
        {
            return LinkReach::UNAVAILABLE;
        }
        if (routerAnswers)
        {
            _scheduler.delayMs(ARP_ANSWER_MS);
            return LinkReach::ANSWERED;
        }
        _scheduler.delayMs(waitMs);
        return LinkReach::SILENT;
    }

    LinkReach echo(uint32_t address, uint32_t waitMs) override
    {
        (void)address;
        echoes++;
        if (!_wifi.isConnected())
        {
            return LinkReach::UNAVAILABLE;
        }
        if (routerAnswers && internetAnswers)
        {
            _scheduler.delayMs(ECHO_ANSWER_MS);
            return LinkReach::ANSWERED;
        }
        _scheduler.delayMs(waitMs);
        return LinkReach::SILENT;
    }

    static const uint32_t ARP_ANSWER_MS = 5;
    static const uint32_t ECHO_ANSWER_MS = 30;

private:
    IWifiAdapter& _wifi;
    ITaskScheduler& _scheduler;
};
