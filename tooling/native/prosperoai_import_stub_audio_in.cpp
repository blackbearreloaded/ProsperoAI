// SPDX-License-Identifier: GPL-3.0-or-later
// Link metadata only. The app imports the console's resident libSceAudioIn.
extern "C"
{
    int sceAudioInInit()
    {
        return 0;
    }
    int sceAudioInOpen(int, unsigned, unsigned, unsigned, unsigned, unsigned)
    {
        return 0;
    }
    int sceAudioInInput(int, void *)
    {
        return 0;
    }
    int sceAudioInClose(int)
    {
        return 0;
    }
}
