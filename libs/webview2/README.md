# Microsoft Edge WebView2 SDK

Minimal native x64 and ARM64 subset of `Microsoft.Web.WebView2` 1.0.4129.50, downloaded from:

`https://www.nuget.org/api/v2/package/Microsoft.Web.WebView2/1.0.4129.50`

NuGet package SHA-256:

`d3934f482d484b89fb4825df720c710664e1143a1e90f7b3a60794ef33f473d2`

Retained files:

- `WebView2.h`
- `WebView2EnvironmentOptions.h`
- `EventToken.h`, the minimal `EventRegistrationToken` ABI declaration required by
	`WebView2.h` when building outside Visual C++; it matches the Windows SDK definition at
	`microsoft/win32metadata` commit `d0f363037c6987790b3548b7f82f382034d86bf2`
- `WebView2Loader.dll` from `runtimes/win-x64/native`
- `arm64/WebView2Loader.dll` from `runtimes/win-arm64/native`
- `LICENSE.txt`

Loader SHA-256 checksums:

- x64: `a9a09232c25805323d4cfb3fc8f545a190a9c8a99c93262ea99d0b88df99ec90`
- ARM64: `b0bfa03347a00169903c4ef0c27579dd9e85236a6dcd637a941d20b86eeec8fc`

Do not retain the 10.7 MB `WebView2LoaderStatic.lib`: it uses the MSVC C++/CRT ABI and does not safely link with LLVM-MinGW. Liteapp embeds the matching loader DLL in its executable, extracts it to a versioned per-user cache, and loads it by absolute path. The Evergreen WebView2 Runtime remains a Microsoft-serviced system dependency and is not bundled.
