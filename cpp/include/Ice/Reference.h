// **********************************************************************
//
// Copyright (c) 2002
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#ifndef ICE_REFERENCE_H
#define ICE_REFERENCE_H

#include <Ice/ReferenceF.h>
#include <Ice/EndpointF.h>
#include <Ice/InstanceF.h>
#include <Ice/Shared.h>

namespace __Ice
{

class ICE_API ReferenceI : public Shared
{
public:

    ReferenceI(const Instance&, const std::string&,
	       const std::vector<TcpEndpoint>&,
	       const std::vector<UdpEndpoint>&);
    virtual ~ReferenceI();

    //
    // All  members are const, because References are immutable.
    //
    const Instance instance;
    const std::string identity;

    enum Mode
    {
	ModeTwoway,
	ModeOneway,
	ModeSecure,
	ModeDatagram
    };
    const Mode mode;

    const std::vector<TcpEndpoint> tcpEndpoints;
    const std::vector<UdpEndpoint> udpEndpoints;

    //
    // Get a new reference, based on the existing one, overwriting
    // certain values.
    //
    Reference changeTimeout(int) const;
    Reference changeMode(Mode) const;
 
    bool operator==(const ReferenceI&) const;
    bool operator!=(const ReferenceI&) const;
    bool operator<(const ReferenceI&) const;

private:

    ReferenceI(const ReferenceI&);
    void operator=(const ReferenceI&);
};

}

#endif
