// **********************************************************************
//
// Copyright (c) 2001
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#ifndef ICE_RSA_KEY_PAIR_F_H
#define ICE_RSA_KEY_PAIR_F_H

#include <Ice/Handle.h>

#ifdef _WIN32
#   ifdef ICE_API_EXPORTS
#       define ICE_API __declspec(dllexport)
#   else
#       define ICE_API __declspec(dllimport)
#   endif
#else
#   define ICE_API /**/
#endif

namespace IceSSL
{

namespace OpenSSL
{

class RSAKeyPair;
typedef IceInternal::Handle<RSAKeyPair> RSAKeyPairPtr;

}

}

namespace IceInternal
{

void ICE_API incRef(::IceSSL::OpenSSL::RSAKeyPair*);
void ICE_API decRef(::IceSSL::OpenSSL::RSAKeyPair*);

}

#endif
