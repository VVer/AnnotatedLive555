/**********
This library is free software; you can redistribute it and/or modify it under
the terms of the GNU Lesser General Public License as published by the
Free Software Foundation; either version 2.1 of the License, or (at your
option) any later version. (See <http://www.gnu.org/copyleft/lesser.html>.)

This library is distributed in the hope that it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for
more details.

You should have received a copy of the GNU Lesser General Public License
along with this library; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301  USA
**********/
// "mTunnel" multicast access service
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// Network Addresses
// C++ header

#ifndef _NET_ADDRESS_HH
#define _NET_ADDRESS_HH

#ifndef _HASH_TABLE_HH
#include "HashTable.hh"
#endif

#ifndef _NET_COMMON_H
#include "NetCommon.h"
#endif

#ifndef _USAGE_ENVIRONMENT_HH
#include "UsageEnvironment.hh"
#endif

// Definition of a type representing a low-level network address.
// At present, this is 32-bits, for IPv4.  Later, generalize it,
// to allow for IPv6.
//netAddressBits是32位的无符号整型数字，相当于网络字节顺序的long
typedef u_int32_t netAddressBits;
//网络IP地址
class NetAddress {
    public:
	NetAddress(u_int8_t const* data,
		   unsigned length = 4 /* default: 32 bits */);
	NetAddress(unsigned length = 4); // sets address data to all-zeros 把地址置为0:0:0:0
	NetAddress(NetAddress const& orig);		   //复制构造函数
	NetAddress& operator=(NetAddress const& rightSide);
	virtual ~NetAddress();

	unsigned length() const { return fLength; }
	u_int8_t const* data() const // always in network byte order
		{ return fData; }

    private:
	void assign(u_int8_t const* data, unsigned length);	//给fData和fLength赋值
	void clean();	  //释放fData占用的空间，并将fLength设为0；

	unsigned fLength;
	u_int8_t* fData;
};
 //地址列表：
//如果host那么是ip地址列表，则NetAddressList只有一个元素。
//如果hostname是一个域名或者主机名的话，就可能有多个地址，
//如“www.google.com”,这个域名可能对应着多个IP地址，此时 NetAddressList就会存储这多个IP地址
class NetAddressList {
    public:
	NetAddressList(char const* hostname);
	NetAddressList(NetAddressList const& orig);
	NetAddressList& operator=(NetAddressList const& rightSide);
	virtual ~NetAddressList();

	unsigned numAddresses() const { return fNumAddresses; }

	NetAddress const* firstAddress() const;

	// Used to iterate through the addresses in a list:
	class Iterator {
	    public:
		Iterator(NetAddressList const& addressList);
		NetAddress const* nextAddress(); // NULL iff none
	    private:
		NetAddressList const& fAddressList;
		unsigned fNextIndex;
	};

    private:
	void assign(netAddressBits numAddresses, NetAddress** addressArray);
	void clean();

	friend class Iterator;
	unsigned fNumAddresses;
	NetAddress** fAddressArray;	 //	fAddressArray是一个指针，指向一个NetAddress数组
};

typedef u_int16_t portNumBits;
//用来将主机字节顺序转化为网络字节顺序，bigendian
class Port {
    public:
	Port(portNumBits num /* in host byte order */);

	portNumBits num() const // in network byte order
		{ return fPortNum; }

    private:
	portNumBits fPortNum; // stored in network byte order
#ifdef IRIX
	portNumBits filler; // hack to overcome a bug in IRIX C++ compiler
#endif
};

UsageEnvironment& operator<<(UsageEnvironment& s, const Port& p);


// A generic table for looking up objects by (address1, address2, port)
//一个通用的hash表，并且键是由三个元素组成的。
class AddressPortLookupTable {
    public:
	AddressPortLookupTable();
	virtual ~AddressPortLookupTable();

	void* Add(netAddressBits address1, netAddressBits address2,
		  Port port, void* value);
		// Returns the old value if different, otherwise 0
	Boolean Remove(netAddressBits address1, netAddressBits address2,
		       Port port);
	void* Lookup(netAddressBits address1, netAddressBits address2,
		     Port port);
		// Returns 0 if not found

	// Used to iterate through the entries in the table
	class Iterator {
	    public:
		Iterator(AddressPortLookupTable& table);
		virtual ~Iterator();

		void* next(); // NULL iff none

	    private:
		HashTable::Iterator* fIter;
	};

    private:
	friend class Iterator;
	HashTable* fTable;
};


Boolean IsMulticastAddress(netAddressBits address);


// A mechanism for displaying an IPv4 address in ASCII.  This is intended to replace "inet_ntoa()", which is not thread-safe.
//将网络地址转换成以“.”点隔的字符串格式。	   n (network) to a (ASCII)
class AddressString {
public:
  AddressString(struct sockaddr_in const& addr);
  AddressString(struct in_addr const& addr);
  AddressString(netAddressBits addr); // "addr" is assumed to be in host byte order here

  virtual ~AddressString();

  char const* val() const { return fVal; }

private:
  void init(netAddressBits addr); // used to implement each of the constructors

private:
  char* fVal; // The result ASCII string: allocated by the constructor; deleted by the destructor
};

#endif
