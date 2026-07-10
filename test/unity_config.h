#ifndef UNITY_CONFIG_H
#define UNITY_CONFIG_H

// Provided so the bundled Unity test framework compiles a `main()` runner for
// the native (host) environment. Without UNITY_MAIN, libUnity.a has no
// entry point and the native test program fails to link.
#define UNITY_MAIN

#endif // UNITY_CONFIG_H
