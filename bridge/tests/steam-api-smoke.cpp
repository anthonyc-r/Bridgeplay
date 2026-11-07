#include <windows.h>
#include <cstdio>
#include "isteamclient.h"
#include "isteamuser.h"

int main(int argc, char **argv) {
    const char *module = argc > 1 ? argv[1] : "lsteamclient.dll";
    HMODULE library = LoadLibraryA(module);
    if (!library) { std::fprintf(stderr, "LoadLibrary %s failed: %lu\n", module, GetLastError()); return 1; }
    typedef void *(__cdecl *Factory)(const char *, int *);
    Factory factory = reinterpret_cast<Factory>(GetProcAddress(library, "CreateInterface"));
    if (!factory) { std::fprintf(stderr, "CreateInterface export absent\n"); return 2; }
    int error = 0;
    ISteamClient *client = static_cast<ISteamClient *>(factory(STEAMCLIENT_INTERFACE_VERSION, &error));
    if (!client) { std::fprintf(stderr, "SteamClient023 unavailable: %d\n", error); return 3; }
    HSteamPipe pipe = client->CreateSteamPipe();
    if (!pipe) { std::fprintf(stderr, "CreateSteamPipe failed\n"); return 4; }
    HSteamUser user = client->ConnectToGlobalUser(pipe);
    if (!user) { client->BReleaseSteamPipe(pipe); std::fprintf(stderr, "ConnectToGlobalUser failed\n"); return 5; }
    ISteamUser *steamUser = client->GetISteamUser(user, pipe, STEAMUSER_INTERFACE_VERSION);
    bool loggedOn = steamUser && steamUser->BLoggedOn();
    client->ReleaseUser(pipe, user);
    bool released = client->BReleaseSteamPipe(pipe);
    std::printf("BRIDGEPLAY_INTEL_STEAM_API bits=%u connected=1 logged_on=%u released=%u\n",
        unsigned(sizeof(void *) * 8), unsigned(loggedOn), unsigned(released));
    return steamUser && released ? 0 : 6;
}
