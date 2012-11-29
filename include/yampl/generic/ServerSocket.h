#ifndef YAMPL_MOSERVERSOCKET_H
#define YAMPL_MOSERVERSOCKET_H

#include <tr1/memory>
#include <tr1/functional>
#include <vector>
#include <map>

#include "yampl/ISocket.h"
#include "yampl/ISocketFactory.h"
#include "yampl/Channel.h"
#include "yampl/utils/RawPipe.h"
#include "yampl/utils/Thread.h"
#include "yampl/utils/Poller.h"
#include "yampl/utils/SpinLock.h"
#include "yampl/utils/UUID.h"

namespace yampl{

template <typename T>
class ServerSocket: public ISocket{
  public:
    ServerSocket(const Channel& channel, Semantics semantics, void (*deallocator)(void *, void *), const std::tr1::function<void(T*)> &accept);
    virtual ~ServerSocket();

    virtual void send(SendArgs &args);
    virtual ssize_t recv(RecvArgs &args) = 0;

  protected:
    typedef std::map<std::string, T*> IdToPeerMap;
    typedef std::map<T*, std::string> PeerToIdMap;

    T *m_currentPeer;
    SpinLock m_lock;
    IdToPeerMap m_idToPeer;
    PeerToIdMap m_peerToId;
    std::vector<std::tr1::shared_ptr<T> > m_peers;

  private:
    ServerSocket(const ServerSocket &);
    ServerSocket & operator=(const ServerSocket &);

    void listenerThreadFun(const Channel &channel, Semantics semantics, void (*deallocator)(void *, void *));

    bool m_destroy;
    std::tr1::shared_ptr<Thread> m_listener;
    std::tr1::function<void(T*)> m_accept; // needed because we can't call a virtual function in the constructor
};

template <typename T>
inline ServerSocket<T>::ServerSocket(const Channel& channel, Semantics semantics, void (*deallocator)(void *, void *), const std::tr1::function<void(T*)> &accept) : m_currentPeer(0), m_destroy(false), m_accept(accept){
  m_listener.reset(new Thread(std::tr1::bind(&ServerSocket::listenerThreadFun, this, channel, semantics, deallocator)));
}

template <typename T>
inline ServerSocket<T>::~ServerSocket(){
  m_destroy = true;
  m_listener->cancel();
  m_listener->join();
}

template <typename T>
void ServerSocket<T>::send(SendArgs &args){
  T *peer;

  if(args.peerId){
    typename IdToPeerMap::iterator elem;

    if((elem = m_idToPeer.find(*args.peerId)) != m_idToPeer.end()){
      peer = elem->second;
    }else{
      throw UnroutableException();
    }
  }else if(!m_currentPeer){
    throw UnroutableException();
  }else{
    peer = m_currentPeer;
  }

  peer->send(args);
}

template <typename T>
void ServerSocket<T>::listenerThreadFun(const Channel &channel, Semantics semantics, void (*deallocator)(void *, void *)){
  Poller poller;
  RawPipe listener("/tmp/" + channel.name + "_announce");

  poller.add(listener.getReadFD());

  while(!m_destroy){
    if(poller.poll() == -1)
      continue;

    UUID id = UUID::readFrom(listener);
    Channel peerChannel(channel.name + "_" + (std::string)id);
    std::tr1::shared_ptr<T> peer(new T(peerChannel, semantics, deallocator));

    m_lock.lock();
    m_peers.push_back(peer);
    m_idToPeer[id] = peer.get();
    m_peerToId[peer.get()] = id;
    m_lock.unlock();

    m_accept(peer.get());
  }
}

}

#endif
