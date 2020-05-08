//
// Created by palulukan on 5/2/20.
//

#include "EventLoop.h"

#include "Utils.h"
#include "Log.h"

#include <utility>
#include <cassert>
#include <chrono>
#include <climits>

EventLoop::EventLoop(std::function<void(EventLoopBase&)>  cbInit,
                     std::shared_ptr<IOMuxBase> spFDMux) :
    m_cbInit(std::move(cbInit)),
    m_spIOMux(std::move(spFDMux))
{
    assert(m_spIOMux);
}

EventLoop::~EventLoop(void)
{
    // TODO: _detach all events
}

bool EventLoop::Add(const std::shared_ptr<Event> &spEvent)
{
    assert(spEvent);

    if(spEvent->_isAttached())
    {
        ERROR("Event is already attached to the event loop");
        return false;
    }

    auto evInfo = std::make_unique<_eventInfo>(spEvent, this);

    if(!_updateIOFD((EventHandle)evInfo.get()))
        return false;

    CATCH_ALL(spEvent->_onAttach(this, (EventHandle)evInfo.get()));

    m_lstEvents.push_back(std::unique_ptr<_eventInfo>(evInfo.release()));
    auto itEvent = --m_lstEvents.end();
    (*itEvent)->itEvent = itEvent;

    return true;
}

void EventLoop::Run(void)
{
    if(m_cbInit)
    {
        m_cbInit(*this);
        _processPendingRemovals();
    }

    while(_tick());
}

void EventLoop::_removeEvent(EventLoopBase::EventHandle hEvent)
{
    assert(hEvent);

    if(!_isRegistered(hEvent))
    {
        ERROR("Attempting to remove unregistered event");
        return;
    }

    m_vecRMPending.push_back(hEvent);

    auto *pEventInfo = (_eventInfo *)hEvent;
    pEventInfo->flags &= EVF_DETACHED;

    CATCH_ALL(pEventInfo->spEvent->_onDetached());

    if(pEventInfo->fdRegistered >= 0)
        m_spIOMux->Unregister(pEventInfo->fdRegistered);
}

void EventLoop::_notifyIOStateChange(EventLoopBase::EventHandle hEvent)
{
    assert(hEvent);

    if(!_isRegistered(hEvent))
    {
        ERROR("Attempting to update IO state of unregistered event");
        return;
    }

    _updateIOFD(hEvent);
}

void EventLoop::_setTimeout(EventLoopBase::EventHandle hEvent, EventLoopBase::TimeInterval timeout)
{
    assert(hEvent);

    if(!_isRegistered(hEvent))
    {
        ERROR("Attempting to set timeout from unregistered event");
        return;
    }

    auto pEventInfo = (_eventInfo *)hEvent;

    if(pEventInfo->flags & EVF_TIMEOUT_SET)
    {
        ERROR("Event timeout is already set");
        return;
    }

    pEventInfo->tpNextTimeout = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeout);
    pEventInfo->flags |= EVF_TIMEOUT_SET;

    m_qTimeouts.push(hEvent);
}

void EventLoop::_cancelTimeout(EventLoopBase::EventHandle hEvent)
{
    UNUSED_ARG(hEvent);

    // TODO: Implement

    assert(false);
}

bool EventLoop::_isRegistered(EventLoopBase::EventHandle hEvent)
{
    assert(hEvent);

    auto pEventInfo = (_eventInfo *)hEvent;
    return pEventInfo->pEventLoop == this && !(pEventInfo->flags & EVF_DETACHED);
}

bool EventLoop::_tick(void)
{
    // Phase 1: Run timers
    auto now = std::chrono::steady_clock::now();

    while(!m_qTimeouts.empty())
    {
        auto pEventInfo = (_eventInfo *)m_qTimeouts.top();
        if(pEventInfo->tpNextTimeout <= now)
        {
            m_qTimeouts.pop();

            pEventInfo->flags &= ~EVF_TIMEOUT_SET;
            CATCH_ALL(pEventInfo->spEvent->OnTimeout());
        }
        else
            break;
    }

    _processPendingRemovals();

    // Phase 2: Poll file descriptors for IO events
    now = std::chrono::steady_clock::now();

    int nTimeout = m_lstEvents.empty() ? 0 : -1;
    if(!m_qTimeouts.empty())
    {
        auto tSleep = std::chrono::duration_cast<std::chrono::milliseconds>(((_eventInfo *)m_qTimeouts.top())->tpNextTimeout - now).count();
        if(tSleep >= 0)
            nTimeout = tSleep > INT_MAX ? INT_MAX : int(tSleep);
        else
            nTimeout = 0;
    }

    std::vector<std::pair<EventLoopBase::EventHandle, unsigned int>> vecEvents;
    if(!m_spIOMux->Poll(nTimeout, vecEvents))
    {
        ERROR("Critical error: Failed to poll for IO events. Breaking event loop!");
        return false;
    }
    for(const auto& ev : vecEvents)
    {
        auto pEventInfo = (_eventInfo *)ev.first;
        unsigned int eventMask = ev.second;

        if(eventMask & IOMuxBase::IOEV_ERROR)
            CATCH_ALL(pEventInfo->spEvent->OnError());

        if(eventMask & IOMuxBase::IOEV_READ)
            CATCH_ALL(pEventInfo->spEvent->OnRead());

        if(eventMask & IOMuxBase::IOEV_WRITE)
            CATCH_ALL(pEventInfo->spEvent->OnWrite());

        // Report IOEV_CLOSE after IOEV_READ and IOEV_WRITE because there may still be
        // some data ready for read
        if(eventMask & IOMuxBase::IOEV_CLOSE)
            CATCH_ALL(pEventInfo->spEvent->OnClose());
    }

    _processPendingRemovals();

    // TODO: Implement

    return !m_lstEvents.empty();
}

void EventLoop::_processPendingRemovals(void)
{
    _drainNextTickQueue();

    for(auto hEvent : m_vecRMPending)
    {
        auto pEventInfo = (_eventInfo *)hEvent;
        m_lstEvents.erase(pEventInfo->itEvent);
    }

    m_vecRMPending.clear();
}

bool EventLoop::_updateIOFD(EventLoopBase::EventHandle hEvent)
{
    auto pEventInfo = (_eventInfo *)hEvent;

    int fdCurrent = pEventInfo->spEvent->GetFD();
    unsigned int nMaskCurrent = pEventInfo->spEvent->GetIOEventMask();

    if(pEventInfo->fdRegistered != fdCurrent)
    {
        if(pEventInfo->fdRegistered >= 0)
        {
            m_spIOMux->Unregister(pEventInfo->fdRegistered);

            pEventInfo->fdRegistered = -1;
            pEventInfo->nIOEventMask = 0;
        }

        if(fdCurrent >= 0 && !m_spIOMux->Register(fdCurrent, nMaskCurrent, hEvent))
        {
            ERROR("Failed to register IO event in the mux");
            return false;
        }

        pEventInfo->fdRegistered = fdCurrent;
        pEventInfo->nIOEventMask = nMaskCurrent;
    }
    else if(pEventInfo->nIOEventMask != nMaskCurrent && fdCurrent >= 0 && !m_spIOMux->UpdateIOEventMask(fdCurrent, nMaskCurrent, hEvent))
    {
        ERROR("Failed to update IO event mask");
        return false;
    }

    return true;
}
