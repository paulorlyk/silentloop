//
// Created by palulukan on 5/16/20.
//

#ifndef READABLE_H_CE26D04EB976448284AF930B36622C4A
#define READABLE_H_CE26D04EB976448284AF930B36622C4A

#include "Stream.h"

namespace stream
{

class Readable : public virtual Stream
{
    EXPORT_EVENT(data, const std::vector<uint8_t>& vecData)
    EXPORT_EVENT(end)
        //EXPORT_EVENT(pause)
        //EXPORT_EVENT(resume)
        //EXPORT_EVENT(readable)

    public:
        //bool isPaused(void);
        void pause(void);
        //void pipe(Writable& ws);
        //void unpipe(Writable& ws);
        //std::vector<uint8_t> read(size_t size = 0);
        //bool isReadable(void);
        //std::string getReadableEncoding(void);
        //void setEncoding(std::string)
        //bool isReadableEnded(void);
        //bool isReadableFlowing(void);
        void resume(void);
        //void unshift(const std::string& strData);
        //void unshift(const std::vector<uint8_t>& vecData);

    protected:
        // Notifies that stream was switched to the readable state.
        // Implementation should start producing new data after handling this event.
        virtual void _read(void) = 0;

    protected:
        // Must be called to notify stream about new data available.
        // Returns true if stream is still readable and ready to accept
        // new data. Otherwise implementation should stop producing new
        // data until notified by _read() event. This function always succeeds
        // no matter what value it returns. Incoming data will be buffered
        // if this function was called when stream is not in the readable state.
        bool _push(const void* buf, size_t size);

    private:
        void _emitData(void);

    protected:
        bool m_bFlowing = false;

    private:
        std::vector<uint8_t> m_rdBuffer;
        bool m_bEnded = false;
        bool m_bEndEmitted = false;
};

}   // namespace stream

#endif //READABLE_H_CE26D04EB976448284AF930B36622C4A
