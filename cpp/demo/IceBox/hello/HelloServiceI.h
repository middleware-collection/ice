// **********************************************************************
//
// Copyright (c) 2002
// MutableRealms, Inc.
// Huntsville, AL, USA
//
// All Rights Reserved
//
// **********************************************************************

#ifndef HELLO_SERVICE_I_H
#define HELLO_SERVICE_I_H

#include <IceBox/IceBox.h>

#if defined(_WIN32)
#   define HELLO_API __declspec(dllexport)
#else
#   define HELLO_API /**/
#endif

class HELLO_API HelloServiceI : public ::IceBox::Service
{
public:

    HelloServiceI();
    virtual ~HelloServiceI();

    virtual void init(const ::std::string&,
                      const ::Ice::CommunicatorPtr&,
                      const ::Ice::PropertiesPtr&,
                      const ::Ice::StringSeq&);

    virtual void start();

    virtual void stop();

private:

    ::Ice::ObjectAdapterPtr _adapter;
};

#endif
