#ifndef NOTPROTON_UTIL_ARCH_H
#define NOTPROTON_UTIL_ARCH_H

// The existing instruction fixtures exercise ARM decoding on either test host.
#if defined(__x86_64__) && !defined(NP_TEST_ARM64)
#define NP_INTEL 1
#define NP_SIGNATURE_ARCH "x86_64"
#else
#define NP_INTEL 0
#define NP_SIGNATURE_ARCH "arm64"
#endif

#endif
