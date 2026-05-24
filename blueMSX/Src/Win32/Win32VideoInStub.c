/* Stub implementations of videoIn/archVideoIn for x64 builds.
 * Win32VideoIn.cpp depends on STRMBASE.lib which is x86-only.
 * These stubs allow the x64 build to link while disabling video-in capture. */
#ifdef _WIN64

#include "Win32VideoIn.h"
#include "ArchVideoIn.h"

void videoInInitialize(Properties* properties) { (void)properties; }
void videoInCleanup(Properties* properties)    { (void)properties; }
int  videoInGetCount()                          { return 0; }
const char* videoInGetName(int index)           { (void)index; return ""; }
int  videoInGetActive()                         { return -1; }
void videoInSetActive(int index)                { (void)index; }
int  videoInIsActive(int index)                 { (void)index; return 0; }

int     archVideoInIsVideoConnected()                      { return 0; }
UInt16* archVideoInBufferGet(int width, int height)        { (void)width; (void)height; return 0; }

#endif /* _WIN64 */
