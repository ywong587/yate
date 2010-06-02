/**
 * sigtransport.cpp
 * This file is part of the YATE Project http://YATE.null.ro
 *
 * SIGTRAN transports provider, supports SCTP, TCP, UDP, UNIX sockets
 *
 * Yet Another Telephony Engine - a fully featured software PBX and IVR
 * Copyright (C) 2009-2010 Null Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <yatephone.h>
#include <yatesig.h>

#define MAX_BUF_SIZE  48500

using namespace TelEngine;
namespace { // anonymous

class Transport;
class TransportWorker;
class TransportThread;
class SockRef;
class MessageReader;
class TReader;
class StreamReader;
class ListenThread;
class TransportModule;

class TransportWorker
{
    friend class TransportThread;
public:
    inline TransportWorker()
	: m_thread(0)
	{ }
    virtual ~TransportWorker() { stop(); }
    virtual bool readData() = 0;
    virtual bool connectSocket() = 0;
    virtual bool needConnect() = 0;
    bool running();
    bool start(Thread::Priority prio = Thread::Normal);
    inline void resetThread()
	{ m_thread = 0; }
protected:
    void stop();
private:
    TransportThread* m_thread;
};

class SockRef : public RefObject
{
public:
    inline SockRef(Socket** sock)
	: m_sock(sock)
	{ }
    void* getObject(const String& name) const
    {
	if (name == "Socket*")
	    return m_sock;
	return RefObject::getObject(name);
    }
private:
    Socket** m_sock;
};

class TransportThread : public Thread
{
    friend class TransportWorker;
public:
    inline TransportThread(TransportWorker* worker, Priority prio = Normal)
	: Thread("SignallingTransporter",prio), m_worker(worker)
	{ }
    virtual ~TransportThread();
    virtual void run();
private:
    TransportWorker* m_worker;
};

class ListenerThread : public Thread
{
public:
    bool init(const NamedList& param);
    ListenerThread(Transport* trans);
    ~ListenerThread();
    inline void terminate()
	{ cancel(); }
    virtual void run();
    bool addAddress(const NamedList &param);
private:
    Socket* m_socket;
    Transport* m_transport;
    bool m_stream;
};

class TReader : public TransportWorker, public Mutex
{
public:
    inline TReader()
	: Mutex(true,"TReader"), m_canSend(true), m_isSending(false)
	{ }
    virtual ~TReader();
    virtual void listen(int maxConn) = 0;
    virtual bool sendMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0) = 0;
    virtual void setSocket(Socket* s) = 0;
    bool m_canSend;
    bool m_isSending;
};

class Transport : public SIGTransport
{
public:
    enum TransportType {
	None = 0,
	Sctp,
	// All the following transports are not standard
	Tcp,
	Udp,
	Unix,
    };
    enum State {
	Up,
	Initiating,
	Down
    };
    virtual bool initialize(const NamedList* config);
    Transport(const NamedList& params);
    ~Transport();
    inline unsigned char getVersion(unsigned char* buf) const
	{ return buf[0]; }
    inline unsigned char getType(unsigned char* buf) const
	{ return buf[3]; }
    inline unsigned char getClass(unsigned char* buf) const
	{ return buf[2]; }
    inline int transType() const
	{ return m_type; }
    inline bool listen() const
	{ return m_listener != 0; }
    inline bool streamDefault() const
	{ return m_type == Tcp || m_type == Unix; }
    void setStatus(int status);
    inline int status() const
	{ return m_state; }
    inline void resetListener()
	{ m_listener = 0; }
    bool addSocket(Socket* socket,SocketAddr& adress);
    virtual bool reliable() const
	{ return m_type == Sctp || m_type == Tcp; }
    virtual bool control(const NamedList &param);
    virtual bool connected(int id) const
	{ return true;}
    virtual void attached(bool ual)
	{ }
    bool connectSocket();
    u_int32_t getMsgLen(unsigned char* buf);
    bool addAddress(const NamedList &param, Socket* socket);
    bool bindSocket();
    static SignallingComponent* create(const String& type,const NamedList& params);
    virtual bool transmitMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0);
private:
    TReader* m_reader;
    bool m_streamer;
    int m_type;
    int m_state;
    ListenerThread* m_listener;
    const NamedList m_config;
    bool m_endpoint;
};

class StreamReader : public TReader
{
public:
    StreamReader(Transport* transport, Socket* sock);
    ~StreamReader();
    virtual bool readData();
    virtual bool connectSocket()
	{ return m_transport->connectSocket(); }
    virtual bool needConnect()
	{ return m_transport && m_transport->status() == Transport::Down && !m_transport->listen(); }
    virtual bool sendMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0);
    virtual void setSocket(Socket* s);
    virtual void listen(int maxConn)
	{ }
private:
    Transport* m_transport;
    Socket* m_socket;
    DataBlock m_sendBuffer;
    DataBlock m_recvBuffer;
    DataBlock m_headerBuffer;
    int m_headerLen;
    DataBlock m_readBuffer;
    u_int32_t m_totalPacketLen;
};

class MessageReader : public TReader
{
public:
    MessageReader(Transport* transport, Socket* sock, SocketAddr& remote);
    ~MessageReader();
    virtual bool readData();
    virtual bool connectSocket()
	{ return m_transport->connectSocket(); }
    virtual bool needConnect()
	{ return m_transport && m_transport->status() == Transport::Down && !m_transport->listen(); }
    virtual bool sendMSG(const DataBlock& header, const DataBlock& msg, int streamId = 0);
    virtual void setSocket(Socket* s);
    virtual void listen(int maxConn)
	{ m_socket->listen(maxConn); }
private:
    Socket* m_socket;
    Transport* m_transport;
    SocketAddr m_remote;
};

class TransportModule : public Module
{
public:
    TransportModule();
    ~TransportModule();
    virtual void initialize();
private:
    bool m_init;
};

static TransportModule plugin;
YSIGFACTORY2(Transport);

static const TokenDict s_transType[] = {
    { "none",  Transport::None },
    { "sctp",  Transport::Sctp },
    { "tcp",   Transport::Tcp },
    { "udp",   Transport::Udp },
    { "unix",  Transport::Unix },
    { 0, 0 }
};

static void resolveAddress(const String& addr, String& ip, int& port)
{
    ObjList* o = addr.split(':');
    if (o && o->count() < 2) {
	ip = "0.0.0.0";
	port = 3565;
	return;
    }
    String* host = static_cast<String*>(o->get());
    if (host)
	ip = *host;
    o = o->skipNext();
    String* p = static_cast<String*>(o->get());
    if (p)
	port = p->toInteger();
}

/** ListenerThread class */

ListenerThread::ListenerThread(Transport* trans)
    : Thread("Listener Thread"),
      m_socket(0), m_transport(trans), m_stream(true)
{
    DDebug("Transport Listener:",DebugAll,"Creating ListenerThread (%p)",this);
}

ListenerThread::~ListenerThread()
{
    DDebug("Transport Listener",DebugAll,"Destroying ListenerThread (%p)",this);
    if (m_transport) {
	Debug("Transport Listener",DebugWarn,"Unusual exit");
	m_transport->resetListener();
    }
    m_transport = 0;
    m_socket->terminate();
    delete m_socket;
}

bool ListenerThread::init(const NamedList& param)
{
    m_socket = new Socket();
    bool multi = param.getParam("local1") != 0;
    m_stream = param.getBoolValue("stream",m_transport->streamDefault());
    if (multi && m_transport->transType() != Transport::Sctp) {
	Debug("ListenerThread",DebugWarn,"Socket %s does not suport multihomed",
	    lookup(m_transport->transType(),s_transType));
	return false;
    }
    switch (m_transport->transType()) {
	case Transport::Sctp:
	{
	    Socket* soc = 0;
	    Message m("socket.sctp");
	    SockRef* s = new SockRef(&soc);
	    m.userData(s);
	    if (!(Engine::dispatch(m) && soc)) {
		DDebug("ListenerThread",DebugWarn,"Could not obtain SctpSocket");
		return false;
	    }
	    m_socket = soc;
	    m_socket->create(AF_INET,m_stream ? SOCK_STREAM : SOCK_SEQPACKET,IPPROTO_SCTP);
	    break;
	}
	case Transport::Tcp:
	    m_socket->create(AF_INET,SOCK_STREAM);
	    break;
	case Transport::Udp:
	    m_socket->create(AF_INET,SOCK_DGRAM);
	    break;
	case Transport::Unix:
	    m_socket->create(AF_UNIX,SOCK_STREAM);
	    break;
	default:
	    Debug("ListenerThread",DebugWarn,"Unknown type of socket");
    }
    if (!m_socket->valid()) {
	Debug("ListenerThread",DebugWarn,"Unable to create listener socket: %s",
	    strerror(m_socket->error()));
	return false;
    }
    if (!m_socket->setBlocking(false)) {
	DDebug("ListenerThread",DebugWarn,"Unable to set listener to nonblocking mode");
	return false;
    }
    SocketAddr addr(AF_INET);
    String address, adr = param.getValue("local");
    int port;
    resolveAddress(adr,address,port);
    addr.host(address);
    addr.port(port);
    if (!m_socket->bind(addr)) {
	Debug(DebugWarn,"Unable to bind to %s:%u %s",addr.host().c_str(),addr.port(),strerror(errno));
	return false;
    } else
	DDebug("ListenerThread",DebugAll,"Socket bind to %s:%u",
	    addr.host().c_str(),addr.port());
    if (multi && !addAddress(param))
	return false;
    if (!m_socket->listen(3)) {
	DDebug("ListenerThread",DebugWarn,"Unable to listen on socket: %s", strerror(m_socket->error()));
	return false;
    }
    return true;
}

void ListenerThread::run()
{
    if (!m_stream)
	return;
    for (;;)
    {
	Thread::msleep(50,false);
	if (check(false) || Engine::exiting())
	    break;
	SocketAddr address;
	Socket* newSoc = m_socket->accept(address);
	if (!newSoc) {
	    if (!m_socket->canRetry())
		DDebug("ListenerThread",DebugNote, "Accept error: %s", strerror(m_socket->error()));
	    continue;
	} else {
	    if (!m_transport->addSocket(newSoc,address))
		DDebug("ListenerThread",DebugNote,"Connection rejected for %s",
		    address.host().c_str());
		Debug(DebugStub,"See if should be done peeloff for new incomming connections");
	}
    }
    if (m_transport) {
	m_transport->resetListener();
	m_transport = 0;
    }
}

bool ListenerThread::addAddress(const NamedList &param)
{
    ObjList o;
    for (int i = 1; ; i++) {
	SocketAddr* addr = new SocketAddr(AF_INET);
	String temp = "local";
	temp += i;
	const String* adr = param.getParam(temp);
	if (!adr)
	    break;
	String address;
	int port;
	resolveAddress(*adr,address,port);
	addr->host(address);
	addr->port(port);
	o.append(addr);
    }
    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
    if (!s) {
	Debug("ListenerThread",DebugGoOn,"Failed to cast socket");
	return false;
    }
    if (!s->bindx(o)) {
	DDebug("ListenerThread",DebugNote,"Failed to bindx sctp socket [%p] %s",s,strerror(errno));
	return false;
    } else
	Debug(DebugNote,"Socket binded to %d auxiliar addresses",o.count());
    return true;
}


/** TransportThread class */

TransportThread::~TransportThread()
{
    if (m_worker)
	m_worker->resetThread();
    DDebug(DebugAll,"Destroying Transport Thread [%p]",this);
}

void TransportThread::run()
{
    if (!m_worker)
	return;
    while (true) {
	bool ret = false;
	if (m_worker->needConnect())
	    ret = m_worker->connectSocket();
	else
	    ret = m_worker->readData();
	if (ret)
	    Thread::check(true);
	else
	    Thread::msleep(5,true);
    }
}

TReader::~TReader()
{
    Debug(DebugAll,"Destroying TReader [%p]",this);
}

/** TransportWorker class */

bool TransportWorker::running()
{
    return m_thread && m_thread->running();
}

bool TransportWorker::start(Thread::Priority prio)
{
    if (!m_thread)
	m_thread = new TransportThread(this,prio);
    if (m_thread->running() || m_thread->startup())
	return true;
    m_thread->cancel(true);
    m_thread = 0;
    return false;
}

void TransportWorker::stop()
{
    if (!(m_thread && m_thread->running()))
	return;
    m_thread->cancel();
    for (unsigned int i = 2 * Thread::idleMsec(); i--; ) {
	Thread::msleep(1);
	if (!m_thread)
	    return;
    }
    m_thread->cancel(true);
    m_thread = 0;
}

/**
 * Transport class
 */

SignallingComponent* Transport::create(const String& type, const NamedList& name)
{
    if (type != "SIGTransport")
	return 0;
    Configuration cfg(Engine::configFile("sigtransport"));
    cfg.load();

    const char* sectName = name.getValue("basename");
    NamedList* config = cfg.getSection(sectName);
    if (!config) {
	DDebug(&plugin,DebugWarn,"No section '%s' in configuration",c_safe(sectName));
	return 0;
    }
    return new Transport(*config);
}

Transport::Transport(const NamedList &param)
    : m_reader(0), m_state(Down), m_listener(0), m_config(param), m_endpoint(true)
{
    setName("Transport");
    Debug(this,DebugAll,"Transport created (%p)",this);
}

Transport::~Transport()
{
    if (m_listener)
	m_listener->terminate();
    while (m_listener)
	Thread::yield();
    Debug(this,DebugAll,"Destroying Transport [%p]",this);
    delete m_reader;
}

bool Transport::control(const NamedList &param)
{
    String oper = param.getValue("operation","init");
    if (oper == "init")
	return initialize(&param);
    else if (oper == "add_addr") {
	if (!m_listener) {
	    Debug(this,DebugWarn,"Unable to listen on another address ,listener is missing");
	    return false;
	}
	return m_listener->addAddress(param);
    }
    return false;
}

bool Transport::initialize(const NamedList* params)
{
    Configuration cfg(Engine::configFile("sigtransport"));
    cfg.load();
    const char* sectName = params->getValue("basename");
    NamedList* config = cfg.getSection(sectName);
    if (!config) {
	DDebug(&plugin,DebugWarn,"No section '%s' in configuration",c_safe(sectName));
	return false;
    }
    m_type = lookup(config->getValue("type","sctp"),s_transType);
    m_streamer = config->getBoolValue("stream",streamDefault());
    m_endpoint = config->getBoolValue("endpoint",false);
    if (!m_endpoint && m_streamer) {
	m_listener = new ListenerThread(this);
	if (!m_listener->init(*config)) {
	    DDebug(this,DebugNote,"Unable to start listener");
	    return false;
	}
	m_listener->startup();
	return true;
    }
    if (m_streamer)
	m_reader = new StreamReader(this,0);
    else {
	SocketAddr addr(AF_INET);
	String address, adr = m_config.getValue("remote");
	int port;
	resolveAddress(adr,address,port);
	addr.host(address);
	addr.port(port);
	m_reader = new MessageReader(this,0,addr);
	bindSocket();
	if (m_type == Sctp) {
	    // Send a dummy MGMT NTFY message to create the connection
	    static const unsigned char dummy[8] =
		{ 1, 0, 0, 1, 0, 0, 0, 8 };
	    DataBlock hdr((void*)dummy,8,false);
	    if (m_reader->sendMSG(hdr,DataBlock::empty(),1))
		m_reader->listen(1);
	    hdr.clear(false);
	    setStatus(Up);
	}
    }
    m_reader->start();
    return true;
}

bool Transport::bindSocket()
{
    Socket* socket = new Socket();
    bool multi = m_config.getParam("local1") != 0;
    if (multi && transType() != Transport::Sctp) {
	Debug(this,DebugWarn,"Sockets type %s do not suport multihomed",
	    lookup(transType(),s_transType));
	return false;
    }
    switch (m_type) {
	case Transport::Sctp:
	{
	    Socket* soc = 0;
	    Message m("socket.sctp");
	    SockRef* s = new SockRef(&soc);
	    m.userData(s);
	    if (!(Engine::dispatch(m) && soc)) {
		DDebug("ListenerThread",DebugWarn,"Could not obtain SctpSocket");
		return false;
	    }
	    socket = soc;
	    socket->create(AF_INET,SOCK_SEQPACKET,IPPROTO_SCTP);
	    SctpSocket* sctp = static_cast<SctpSocket*>(socket); 
	    if (!sctp->setStreams(2,2))
		DDebug(this,DebugInfo,"Failed to set sctp stream number");
	    if (!sctp->subscribeEvents())
		DDebug(this,DebugInfo,"Unable to subscribe to Sctp events");
	    int ppid = sigtran() ? sigtran()->payload() : 0;
	    ppid = m_config.getIntValue("payload",ppid);
	    if (ppid > 0)
		sctp->setPayload(ppid);
	    break;
	}
	case Transport::Udp:
	    socket->create(AF_INET,SOCK_DGRAM);
	    break;
	default:
	    DDebug(this,DebugWarn,"Unknown/unwanted type of socket %s",
		lookup(transType(),s_transType,"Unknown"));
    }
    if (!socket->valid()) {
	Debug(this,DebugWarn,"Unable to create listener socket: %s",
	    strerror(socket->error()));
	return false;
    }
    if (!socket->setBlocking(false)) {
	DDebug(this,DebugWarn,"Unable to set listener to nonblocking mode");
	return false;
    }
    SocketAddr addr(AF_INET);
    String address, adr = m_config.getValue("local");
    int port;
    resolveAddress(adr,address,port);
    addr.host(address);
    addr.port(port);
    if (!socket->bind(addr)) {
	Debug(DebugNote,"Unable to bind to %s:%u %s",addr.host().c_str(),addr.port(),strerror(errno));
	return false;
    } else
	DDebug(this,DebugAll,"Socket bind to %s:%u",
	    addr.host().c_str(),addr.port());
    if (multi && !addAddress(m_config,socket))
	return false;
    m_reader->setSocket(socket);
    if (m_type == Transport::Udp)
	setStatus(Up);
    return true;
}

bool Transport::addAddress(const NamedList &param, Socket* socket)
{
    ObjList o;
    for (int i = 1; ; i++) {
	SocketAddr* addr = new SocketAddr(AF_INET);
	String temp = "local";
	temp << i;
	const String* adr = param.getParam(temp);
	if (!adr)
	    break;
	String address;
	int port;
	resolveAddress(*adr,address,port);
	addr->host(address);
	addr->port(port);
	o.append(addr);
    }
    SctpSocket* s = static_cast<SctpSocket*>(socket);
    if (!s) {
	Debug(this,DebugGoOn,"Failed to cast socket");
	return false;
    }
    if (!s->bindx(o)) {
	DDebug(this,DebugNote,"Failed to bindx sctp socket [%p] %s",s,strerror(errno));
	return false;
    } else
	Debug(DebugNote,"Socket binded to %d auxiliar addresses",o.count());
    return true;
}


bool Transport::connectSocket()
{
    if (!m_streamer && !m_endpoint)
	return false;
    Socket* sock = 0;
    switch (m_type){
	case Sctp :
	{
	    Message m("socket.sctp");
	    SockRef* s = new SockRef(&sock);
	    m.userData(s);
	    if (!(Engine::dispatch(m) && sock)) {
		DDebug(this,DebugNote,"Could not obtain SctpSocket");
		return false;
	    }
	    sock->create(AF_INET,m_streamer ? SOCK_STREAM : SOCK_SEQPACKET,IPPROTO_SCTP);
	    SctpSocket* socket = static_cast<SctpSocket*>(sock);
	    if (!socket->setStreams(2,2))
		DDebug(this,DebugInfo,"Failed to set sctp stream number");
	    if (!socket->subscribeEvents())
		DDebug(this,DebugInfo,"Unable to subscribe to Sctp events");
	    break;
	}
	case Tcp :
	    sock = new Socket();
	    sock->create(AF_INET,SOCK_STREAM);
	    break;
	case Udp :
	    sock = new Socket();
	    m_streamer = false;
	    sock->create(AF_INET,SOCK_DGRAM);
	    break;
	case Unix :
	    sock = new Socket();
	    sock->create(AF_UNIX,SOCK_STREAM);
	    break;
	default:
	    DDebug(this,DebugWarn,"Unknown type of socket %s",lookup(m_type,s_transType,"Unknown"));
	    return false;
    }
    if (!m_streamer) {
	SocketAddr addr(AF_INET);
	String address, adr = m_config.getValue("local");
	int port;
	resolveAddress(adr,address,port);
	addr.host(address);
	addr.port(port);
	if (!sock->bind(addr))
	    Debug(DebugNote,"Failed to bind Socket. [%p]",sock);
    }
    if (!m_config.getParam("remote1")) {
	SocketAddr addr(AF_INET);
	String address, adr = m_config.getValue("remote");
	int port;
	resolveAddress(adr,address,port);
	addr.host(address);
	addr.port(port);
	if (m_endpoint && !sock->connect(addr)) {
	    DDebug(this,DebugNote,"Unable to connect to %s:%u. %s",
		addr.host().c_str(),addr.port(),strerror(errno));
	    sock->terminate();
	    delete sock;
	    return false;
	}
    }
    else {
	ObjList o;
	for (unsigned int i = 0; ; i++) {
	    SocketAddr* addr = new SocketAddr(AF_INET);
	    String aux = "remote";
	    if (i)
		aux << i;
	    String* adr = m_config.getParam(aux);
	    if (!adr)
		break;
	    String address;
	    int port;
	    resolveAddress(*adr,address,port);
	    addr->host(address);
	    addr->port(port);
	    o.append(addr);
	}
	SctpSocket* s = static_cast<SctpSocket*>(sock);
	if (!s) {
	    Debug(this,DebugGoOn,"Failed to cast socket");
	    return false;
	}
	if (!s->connectx(o)) {
	    DDebug(this,DebugNote,"Failed to connectx sctp socket [%p]",s);
	    s->terminate();
	    delete s;
	    return false;
	} else
	     Debug(DebugNote,"Socket conected to %d addresses",o.count());
    }
    m_reader->setSocket(sock);
    setStatus(Up);
    return true;
}

void Transport::setStatus(int status)
{
    if (m_state == status)
	return;
    m_state = status;
    m_reader->m_canSend = true;
    notifyLayer((status == Up) ? SignallingInterface::LinkUp : SignallingInterface::LinkDown);
}

u_int32_t Transport::getMsgLen(unsigned char* buf)
{
    return ((unsigned int)buf[4] << 24) | ((unsigned int)buf[5] << 16) | 
		((unsigned int)buf[6] << 8) | (unsigned int)buf[7];
}


bool Transport::transmitMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    if (!m_reader)
	return false;
    bool ret = m_reader->sendMSG(header,msg,streamId);
    return ret;
}

bool Transport::addSocket(Socket* socket,SocketAddr& adress)
{
    if (transType() == Up)
	return false;
    SocketAddr addr(AF_INET);
    String address, adr = m_config.getValue("remote");
    int port;
    resolveAddress(adr,address,port);
    addr.host(address);
    addr.port(port);
    switch (m_type) {
	case Sctp :
	{
	    Socket* sock = 0;
	    Message m("socket.sctp");
	    m.addParam("handle",String(socket->handle()));
	    SockRef* s = new SockRef(&sock);
	    m.userData(s);
	    if (!(Engine::dispatch(m) && sock)) {
		DDebug(this,DebugNote,"Could not obtain SctpSocket");
		return false;
	    }
	    SctpSocket* soc = static_cast<SctpSocket*>(sock);
	    if (!soc->setStreams(2,2))
		DDebug(this,DebugInfo,"Sctp set Streams failed");
	    if (!soc->subscribeEvents())
		DDebug(this,DebugInfo,"Sctp subscribe events failed");
	    if (m_streamer)
		m_reader = new StreamReader(this,soc);
	    else
		m_reader = new MessageReader(this,soc,addr);
	    break;
	}
	case Unix :
	case Tcp :
	    m_reader = new StreamReader(this,socket);
	    break;
	case Udp :
	    m_reader = new MessageReader(this,socket,addr);
	    break;
	default:
	    Debug(this,DebugNote,"Unknown socket type ");
	    return false;
    }
    m_reader->start();
    setStatus(Up);
    return true;
}

/**
 * class StreamReader 
 */

StreamReader::StreamReader(Transport* transport,Socket* sock)
    : m_transport(transport), m_socket(sock), m_headerLen(8), m_totalPacketLen(0)
{
     DDebug(transport,DebugAll,"Creating StreamReader (%p,%p) [%p]",transport,sock,this);
}

StreamReader::~StreamReader()
{
    stop();
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
    }
    DDebug(m_transport,DebugAll,"Destroying StreamReader [%p]",this);
}

void StreamReader::setSocket(Socket* s)
{
    if (!s || s == m_socket)
	return;
    Socket* temp = m_socket;
    m_socket = s;
    if (temp) {
	temp->terminate();
	delete temp;
    }
}

bool StreamReader::sendMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    if (!m_canSend)
	return false;
    m_isSending = true;
    if (((m_sendBuffer.length() + msg.length()) + header.length()) < MAX_BUF_SIZE) {
	m_sendBuffer += header;
	m_sendBuffer += msg;
    }
    else
	Debug(m_transport,DebugAll,"Buffer Overrun");
    while (m_socket && m_sendBuffer.length()) {
	bool sendOk = false, error = false;
	if (m_socket->select(0,&sendOk,&error,Thread::idleUsec())) {
	    if (error) {
		DDebug(m_transport,DebugAll,"Error detected. %s",strerror(errno));
		break;
	    }
	    if (!sendOk)
		break;
	    int len = 0;
	    if (m_transport->transType() == Transport::Sctp) {
		SctpSocket* s = static_cast<SctpSocket*>(m_socket);
		if (!s) {
		    DDebug(m_transport,DebugGoOn,"Sctp conversion failed");
		    break;
		}
		int flags = 0;
		len = s->sendMsg(m_sendBuffer.data(),m_sendBuffer.length(),streamId,flags);
	    }
	    else
		len = m_socket->send(m_sendBuffer.data(),m_sendBuffer.length());
	    if (len <= 0)
		break;
	    m_sendBuffer.cut(-len);
	}
	break;
    }
    m_isSending = false;
    return (m_sendBuffer.length() == 0);
}

bool StreamReader::readData()
{
    if (!m_socket)
	return false;
    bool readOk = false, error = false;
    if (!m_socket->select(&readOk,0,&error,Thread::idleUsec()))
	return false;
    if (!readOk || error || !running())
	return false;
    int stream = 0, len = 0;
    SocketAddr addr;
    unsigned char buf[MAX_BUF_SIZE];
    if (m_headerBuffer.length() < 8) {
	int flags = 0;
	if (m_transport->transType() == Transport::Sctp) {
	    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	    if (!s) {
		DDebug(m_transport,DebugGoOn,"Sctp conversion failed");
		return false;
	    }
	    len = s->recvMsg((void*)buf,m_headerLen,addr,stream,flags);
	    if (flags) {
		if (flags == 2) {
		    Debug(m_transport,DebugInfo,"Sctp commUp");
		    m_transport->setStatus(Transport::Up);
		    return true;
		}
		DDebug(m_transport,DebugWarn,"Connection down [%p] %d",m_socket, flags);
		m_transport->setStatus(Transport::Down);
		while (m_isSending)
		    Thread::yield();
		m_canSend = false;
		m_socket->terminate();
		if (m_transport->listen())
		    stop();
		delete m_socket;
		return false;
	    }
	}
	else {
	    SocketAddr addr;
	    len = m_socket->recv((void*)buf,m_headerLen);
	}
	if (len <= 0)
	    return false;
	m_headerLen -= len;
	m_headerBuffer.append(buf,len);
	if (m_headerLen > 0)
	    return true;
	unsigned char* auxBuf = (unsigned char*)m_headerBuffer.data();
	m_totalPacketLen = m_transport->getMsgLen(auxBuf);
	if (m_totalPacketLen >= 8 && m_totalPacketLen < MAX_BUF_SIZE)
	    m_totalPacketLen -= 8;
	else {
	    DDebug(m_transport,DebugWarn,"Protocol error - unsupported length of packet %d!",
		m_totalPacketLen);
	    return false;
	}
    }
    unsigned char buf1[MAX_BUF_SIZE];
    if (m_totalPacketLen > 0) {
	len = 0;
	int flags = 0;
	if (m_transport->transType() == Transport::Sctp) {
	    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	    if (!s) {
		DDebug(m_transport,DebugGoOn,"Sctp conversion failed");
		return false;
	    }
	    len = s->recvMsg((void*)buf1,m_totalPacketLen,addr,stream,flags);
	    if (flags) {
		DDebug(m_transport,DebugWarn,"Receive error [%p]",m_socket);
		m_transport->notifyLayer(SignallingInterface::HardwareError);
	    }
	}
	else {
	    SocketAddr addr;
	    len = m_socket->recv((void*)buf1,m_totalPacketLen);
	}
	if (len <= 0)
	    return false;
	m_transport->setStatus(Transport::Up);
	m_totalPacketLen -= len;
	m_readBuffer.append(buf1,len);
	if (m_totalPacketLen > 0)
	    return true;
	m_transport->processMSG(m_transport->getVersion((unsigned char*)m_headerBuffer.data()),
	    m_transport->getClass((unsigned char*)m_headerBuffer.data()),m_transport->getType((unsigned char*)
	    m_headerBuffer.data()), m_readBuffer,stream);
	m_totalPacketLen = 0;
	m_readBuffer.clear(true);
	m_headerLen = 8;
	m_headerBuffer.clear(true);
	return true;
    }
    return false;
}

/**
 * class MessageReader
 */

MessageReader::MessageReader(Transport* transport, Socket* sock, SocketAddr& addr)
    : m_socket(sock), m_transport(transport), m_remote(addr)
{
    DDebug(DebugAll,"Creating MessageReader [%p]",this);
}

MessageReader::~MessageReader()
{
    stop();
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
    }
    DDebug(DebugAll,"Destroying MessageReader [%p]",this);
}

void MessageReader::setSocket(Socket* s)
{
    if (!s || s == m_socket)
	return;
    if (m_socket) {
	m_socket->terminate();
	delete m_socket;
    }
    m_socket = s;
}

bool MessageReader::sendMSG(const DataBlock& header, const DataBlock& msg, int streamId)
{
    if (!m_canSend)
	return false;
    bool sendOk = false, error = false;
    bool ret = false;
    m_isSending = true;
    while (m_socket && m_socket->select(0,&sendOk,&error,Thread::idleUsec())) {
	if (error) {
	    DDebug(m_transport,DebugAll,"Send error detected. %s",strerror(errno));
	    m_transport->setStatus(Transport::Down);
	    break;
	}
	if (!sendOk)
	    break;
	int totalLen = header.length() + msg.length();
	if (!totalLen) {
	    ret = true;
	    break;
	}
	DataBlock buf(header);
	buf += msg;
#ifdef XDEBUG
	String aux;
	aux.hexify(buf.data(),buf.length(),' ');
	Debug(m_transport,DebugInfo,"Sending: %s",aux.c_str());
#endif
	int len = 0;
	if (m_transport->transType() == Transport::Sctp) {
	    SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	    if (!s) {
		DDebug(m_transport,DebugGoOn,"Sctp conversion failed");
		break;
	    }
	    int flags = 0;
	    len = s->sendTo(buf.data(),totalLen,streamId,m_remote,flags);
	}
	else
	    len = m_socket->sendTo(buf.data(),totalLen,m_remote);
	if (len == totalLen) {
	    ret = true;
	    break;
	}
	DDebug(m_transport,DebugMild,"Error sending message %d %d %s",len,totalLen,strerror(errno));
	break;
    }
    m_isSending = false;
    return ret;
}

bool MessageReader::readData()
{
    bool readOk = false,error = false;
    if (!(running() && m_socket && m_socket->select(&readOk,0,&error,Thread::idleUsec())))
	return false;
    if (!readOk || error)
	return false;

    unsigned char buffer[MAX_BUF_SIZE];
    int stream = 0;
    int r = 0;
    SocketAddr addr;
    if (m_transport->transType() == Transport::Sctp) {
	int flags = 0;
	SctpSocket* s = static_cast<SctpSocket*>(m_socket);
	if (!s) {
	    DDebug(m_transport,DebugGoOn,"Sctp conversion failed");
	    return false;
	}
	SocketAddr addr;
	r = s->recvMsg((void*)buffer,MAX_BUF_SIZE,addr,stream,flags);
	if (flags) {
	     if (flags == 2) {
		DDebug(m_transport,DebugAll,"Sctp connection is Up");
		m_transport->setStatus(Transport::Up);
		return true;
	    }
	    DDebug(m_transport,DebugNote,"Message error [%p] %d",m_socket,flags);
	    m_transport->setStatus(Transport::Initiating);
	    return false;
	}
    }
    else
	r = m_socket->recvFrom((void*)buffer,MAX_BUF_SIZE,addr);

    if (r <= 0)
	return false;

    u_int32_t len = m_transport->getMsgLen(buffer);
    if ((unsigned int)r != len) {
	Debug(m_transport,DebugNote,"Protocol read error read: %d, expected %d",r,len);
	return false;
    }
    m_transport->setStatus(Transport::Up);
    DataBlock packet(buffer,r);
    packet.cut(-8);
    m_transport->processMSG(m_transport->getVersion((unsigned char*)buffer),
	m_transport->getClass((unsigned char*)buffer), m_transport->getType((unsigned char*)buffer),packet,stream);
    return true;
}

/**
 * class TransportModule
 */

TransportModule::TransportModule()
    : Module("sigtransport","misc",true),
      m_init(false)
{
    Output("Loaded module SigTransport");
}

TransportModule::~TransportModule()
{
    Output("Unloading module SigTransport");
}

void TransportModule::initialize()
{
    if (!m_init) {
	Output("Initializing module SigTransport");
	m_init = true;
	setup();
    }
}

}; // anonymous namespace

/* vi: set ts=8 sw=4 sts=4 noet: */
