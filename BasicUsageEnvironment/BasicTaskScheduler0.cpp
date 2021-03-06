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
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// Basic Usage Environment: for a simple, non-scripted, console application
// Implementation

#include "BasicUsageEnvironment0.hh"
#include "HandlerSet.hh"

////////// A subclass of DelayQueueEntry,
//////////     used to implement BasicTaskScheduler0::scheduleDelayedTask()

class AlarmHandler : public DelayQueueEntry {
public:
	AlarmHandler(TaskFunc* proc, void* clientData, DelayInterval timeToDelay)
		: DelayQueueEntry(timeToDelay), fProc(proc), fClientData(clientData) {
	}

private: // redefined virtual functions
	virtual void handleTimeout() {
		(*fProc)(fClientData);
		DelayQueueEntry::handleTimeout();  // handleTimeout会调用析构函数
	}

private:
	TaskFunc* fProc;
	void* fClientData;
};


////////// BasicTaskScheduler0 //////////

BasicTaskScheduler0::BasicTaskScheduler0()
: fLastHandledSocketNum(-1), fTriggersAwaitingHandling(0), fLastUsedTriggerMask(1), fLastUsedTriggerNum(MAX_NUM_EVENT_TRIGGERS - 1) {
	fHandlers = new HandlerSet;
	for (unsigned i = 0; i < MAX_NUM_EVENT_TRIGGERS; ++i) {
		fTriggeredEventHandlers[i] = NULL;
		fTriggeredEventClientDatas[i] = NULL;
	}
}

BasicTaskScheduler0::~BasicTaskScheduler0() {
	delete fHandlers;
}
//向任务队列中添加一个任务
//参数 ：
//		microseconds：该任务多少毫秒之后执行
//		proc：要执行的任务
//		clientData：proc的参数
//返回值TaskToken对应于fDelayQueue中节点的fToken（ID）
TaskToken BasicTaskScheduler0::scheduleDelayedTask(int64_t microseconds,
	TaskFunc* proc,
	void* clientData) {
	if (microseconds < 0) microseconds = 0;
	DelayInterval timeToDelay((long)(microseconds / 1000000), (long)(microseconds % 1000000));
	AlarmHandler* alarmHandler = new AlarmHandler(proc, clientData, timeToDelay);
	fDelayQueue.addEntry(alarmHandler);

	return (void*)(alarmHandler->token());
}
//删除一个任务
void BasicTaskScheduler0::unscheduleDelayedTask(TaskToken& prevTask) {
	//removeEntry将一个节点从双向链表中删除。
	DelayQueueEntry* alarmHandler = fDelayQueue.removeEntry((intptr_t)prevTask);
	prevTask = NULL;
	delete alarmHandler;
}

void BasicTaskScheduler0::doEventLoop(char* watchVariable) {
	// Repeatedly loop, handling readble sockets and timed events:
	while (1) {
		if (watchVariable != NULL && *watchVariable != 0) break;
		SingleStep();
	}
}
//创建一个触发事件
//返回值EventTriggerId为mask，如果mask为0，表明分配失败，没有可用的槽
EventTriggerId BasicTaskScheduler0::createEventTrigger(TaskFunc* eventHandlerProc) {
	unsigned i = fLastUsedTriggerNum;
	EventTriggerId mask = fLastUsedTriggerMask;

	do {
		i = (i + 1) % MAX_NUM_EVENT_TRIGGERS;
		mask >>= 1;
		if (mask == 0) mask = 0x80000000;

		if (fTriggeredEventHandlers[i] == NULL) {	  //这个slot未分配任务
			// This trigger number is free; use it:
			fTriggeredEventHandlers[i] = eventHandlerProc;
			fTriggeredEventClientDatas[i] = NULL; // sanity

			fLastUsedTriggerMask = mask;
			fLastUsedTriggerNum = i;

			return mask;
		}
	} while (i != fLastUsedTriggerNum);

	// All available event triggers are allocated; return 0 instead:
	return 0;
}
//删除一个触发事件
void BasicTaskScheduler0::deleteEventTrigger(EventTriggerId eventTriggerId) {
	fTriggersAwaitingHandling &= ~eventTriggerId;

	if (eventTriggerId == fLastUsedTriggerMask) { // common-case optimization:
		fTriggeredEventHandlers[fLastUsedTriggerNum] = NULL;
		fTriggeredEventClientDatas[fLastUsedTriggerNum] = NULL;
	}
	else {
		// "eventTriggerId" should have just one bit set.
		// However, we do the reasonable thing if the user happened to 'or' together two or more "EventTriggerId"s:
		EventTriggerId mask = 0x80000000;
		for (unsigned i = 0; i < MAX_NUM_EVENT_TRIGGERS; ++i) {
			if ((eventTriggerId&mask) != 0) {
				fTriggeredEventHandlers[i] = NULL;
				fTriggeredEventClientDatas[i] = NULL;
			}
			mask >>= 1;
		}
	}
}

void BasicTaskScheduler0::triggerEvent(EventTriggerId eventTriggerId, void* clientData) {
	// First, record the "clientData".  (Note that we allow "eventTriggerId" to be a combination of bits for multiple events.)
	EventTriggerId mask = 0x80000000;
	for (unsigned i = 0; i < MAX_NUM_EVENT_TRIGGERS; ++i) {
		if ((eventTriggerId&mask) != 0) {
			fTriggeredEventClientDatas[i] = clientData;
		}
		mask >>= 1;
	}

	// Then, note this event as being ready to be handled.
	// (Note that because this function (unlike others in the library) can be called from an external thread, we do this last, to
	//  reduce the risk of a race condition.)
	//将eventTriggerId的bit位添加到 fTriggersAwaitingHandling中去
	fTriggersAwaitingHandling |= eventTriggerId;
}


////////// HandlerSet (etc.) implementation //////////

HandlerDescriptor::HandlerDescriptor(HandlerDescriptor* nextHandler)
: conditionSet(0), handlerProc(NULL) {
	// Link this descriptor into a doubly-linked list:
	if (nextHandler == this) { // initialization
		fNextHandler = fPrevHandler = this;
	}
	else {
		//在双向链表中插入本节点
		fNextHandler = nextHandler;
		fPrevHandler = nextHandler->fPrevHandler;
		nextHandler->fPrevHandler = this;
		fPrevHandler->fNextHandler = this;
	}
}
//析构函数中会将此节点从双向链表中删除
HandlerDescriptor::~HandlerDescriptor() {
	// Unlink this descriptor from a doubly-linked list:
	fNextHandler->fPrevHandler = fPrevHandler;
	fPrevHandler->fNextHandler = fNextHandler;
}

HandlerSet::HandlerSet()
: fHandlers(&fHandlers) {
	fHandlers.socketNum = -1; // shouldn't ever get looked at, but in case...
}
//HanderSet的析构函数，逐个删除Handlers
HandlerSet::~HandlerSet() {
	// Delete each handler descriptor:
	while (fHandlers.fNextHandler != &fHandlers) {
		delete fHandlers.fNextHandler; // changes fHandlers->fNextHandler
	}
}

void HandlerSet
::assignHandler(int socketNum,
int conditionSet,
TaskScheduler::BackgroundHandlerProc* handlerProc,
void* clientData) {
	// First, see if there's already a handler for this socket:
	HandlerDescriptor* handler = lookupHandler(socketNum);
	if (handler == NULL) { // No existing handler, so create a new descr:
		//创建一个新的HandlerDescriptor节点并将其插在双向链表的头节点fHandlers后面
		handler = new HandlerDescriptor(fHandlers.fNextHandler);
		handler->socketNum = socketNum;
	}

	handler->conditionSet = conditionSet;
	handler->handlerProc = handlerProc;
	handler->clientData = clientData;
}
//删除一个socketNum对应的HandlerDescriptor
void HandlerSet::clearHandler(int socketNum) {
	HandlerDescriptor* handler = lookupHandler(socketNum);
	delete handler; //此操作会调用HandlerDescriptor::~HandlerDescriptor()，在析构函数里面会将此节点从双向链表中删除
}
//给节点的 socketNum重新赋值
void HandlerSet::moveHandler(int oldSocketNum, int newSocketNum) {
	HandlerDescriptor* handler = lookupHandler(oldSocketNum);
	if (handler != NULL) {
		handler->socketNum = newSocketNum;
	}
}
//遍历整个set，查找socketNum为socketNum的HandlerDescriptor
HandlerDescriptor* HandlerSet::lookupHandler(int socketNum) {
	HandlerDescriptor* handler;
	HandlerIterator iter(*this);
	while ((handler = iter.next()) != NULL) {
		if (handler->socketNum == socketNum) break;
	}
	return handler;
}

HandlerIterator::HandlerIterator(HandlerSet& handlerSet)
: fOurSet(handlerSet) {
	reset();
}

HandlerIterator::~HandlerIterator() {
}
//重置：将指针指向头节点后面的第一个节点
void HandlerIterator::reset() {
	fNextPtr = fOurSet.fHandlers.fNextHandler;
}

HandlerDescriptor* HandlerIterator::next() {
	HandlerDescriptor* result = fNextPtr;
	if (result == &fOurSet.fHandlers) { // no more
		result = NULL;
	}
	else {
		fNextPtr = fNextPtr->fNextHandler;
	}

	return result;
}
