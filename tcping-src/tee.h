#ifndef TEE_H
#define TEE_H

#include <fstream>
#include <stdio.h>

/*
Don't make fun of me, Unix guys.  I know about `which tee`.  Tee isn't always available on windows systems, however.

(The class is named Tee rather than tee because POSIX <unistd.h> declares a
function called tee(), which would otherwise collide with the type name.)
 */

class Tee
{
public:
    Tee();
    ~Tee();
    void Open(char* filename);
    void OpenAppend(char* filename);
    void Close();
    void p(const char* text);
    void pf(const char* format, ...);
    void enable(bool onoff);

private:
    std::ofstream outfile;
    int flag;
    bool enable_output;
};

#endif // TEE_H
