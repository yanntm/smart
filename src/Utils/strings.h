
#ifndef STRINGS_H
#define STRINGS_H

#include "../include/shared.h"
#include "../Streams/streams.h"

/** Shared strings.
    Used so we can "share" strings without copying them.
    Plus, any string result will be stored using this class.
*/
class shared_string : public shared_object {
        char* string;
    public:
        /// Constructor, sets string to null.
        shared_string();
        /** Constructor.
            @param  s   String to fill from.  Will be copied.
        */
        shared_string(const char* s);
        unsigned length() const;
    protected:
        virtual ~shared_string();
    public:
//        void CopyFrom(const char* s);
        inline const char* getStr() const { return string; }
#ifdef OLD_STREAMS
        virtual bool Print(OutputStream &s, int width) const;
#else
        virtual bool Print(std::ostream &s, int width) const;
#endif
        virtual bool Equals(const shared_object *o) const;
        int Compare(const shared_string* s) const;
        int Compare(const char* x) const;
};

#endif
