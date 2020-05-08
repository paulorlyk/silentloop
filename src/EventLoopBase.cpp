//
// Created by palulukan on 5/8/20.
//

#include "EventLoopBase.h"

#include <cassert>

void EventLoopBase::_nextTick(const std::function<void(void)> &cb)
{
    assert(cb);

    m_qNextTick.push(cb);
}

void EventLoopBase::_drainNextTickQueue(void)
{
    while(!m_qNextTick.empty())
    {
        m_qNextTick.front()();
        m_qNextTick.pop();
    }
}
