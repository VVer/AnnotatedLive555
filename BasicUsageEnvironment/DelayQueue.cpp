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
// Copyright (c) 1996-2014, Live Networks, Inc.  All rights reserved
//	Help by Carlo Bonamico to get working for Windows
// Delay queue
// Implementation

#include "DelayQueue.hh"
#include "GroupsockHelper.hh"

static const int MILLION = 1000000;

///// Timeval /////

int Timeval::operator>=(const Timeval& arg2) const {
	return seconds() > arg2.seconds()
		|| (seconds() == arg2.seconds()
		&& useconds() >= arg2.useconds());
}

void Timeval::operator+=(const DelayInterval& arg2) {
	secs() += arg2.seconds(); usecs() += arg2.useconds();
	if (useconds() >= MILLION) {
		usecs() -= MILLION;
		++secs();
	}
}

void Timeval::operator-=(const DelayInterval& arg2) {
	secs() -= arg2.seconds(); usecs() -= arg2.useconds();
	if ((int)useconds() < 0) {
		usecs() += MILLION;
		--secs();
	}
	if ((int)seconds() < 0)
		secs() = usecs() = 0;

}

DelayInterval operator-(const Timeval& arg1, const Timeval& arg2) {
	time_base_seconds secs = arg1.seconds() - arg2.seconds();
	time_base_seconds usecs = arg1.useconds() - arg2.useconds();

	if ((int)usecs < 0) {
		usecs += MILLION;
		--secs;
	}
	if ((int)secs < 0)
		return DELAY_ZERO;
	else
		return DelayInterval(secs, usecs);
}


///// DelayInterval /////

DelayInterval operator*(short arg1, const DelayInterval& arg2) {
	time_base_seconds result_seconds = arg1*arg2.seconds();
	time_base_seconds result_useconds = arg1*arg2.useconds();

	time_base_seconds carry = result_useconds / MILLION;
	result_useconds -= carry*MILLION;
	result_seconds += carry;

	return DelayInterval(result_seconds, result_useconds);
}

#ifndef INT_MAX
#define INT_MAX	0x7FFFFFFF
#endif
const DelayInterval DELAY_ZERO(0, 0);
const DelayInterval DELAY_SECOND(1, 0);
const DelayInterval DELAY_MINUTE = 60 * DELAY_SECOND;
const DelayInterval DELAY_HOUR = 60 * DELAY_MINUTE;
const DelayInterval DELAY_DAY = 24 * DELAY_HOUR;
const DelayInterval ETERNITY(INT_MAX, MILLION - 1);
// used internally to make the implementation work


///// DelayQueueEntry /////

intptr_t DelayQueueEntry::tokenCounter = 0;

DelayQueueEntry::DelayQueueEntry(DelayInterval delay)
: fDeltaTimeRemaining(delay) {
	//Next和Prev指针均指向自己
	fNext = fPrev = this;	
	//类变量tokenCounter加1，并将该值赋给当前Entry的fToken
	//fToken可以理解为当前Entry的ID
	fToken = ++tokenCounter;
}

DelayQueueEntry::~DelayQueueEntry() {
}

void DelayQueueEntry::handleTimeout() {
	delete this;
}


///// DelayQueue /////
//DelayQueue继承自DelayQueueEntry，
//所以在它初始化的时候，调用DelayQueueEntry(ETERNITY)
//将fDeltaTimeRemaining设为ETERNITY，
//到一个sentinel的作用
DelayQueue::DelayQueue()
: DelayQueueEntry(ETERNITY) {
	//将当前时刻设为第一次同步的时刻
	fLastSyncTime = TimeNow();
}

DelayQueue::~DelayQueue() {
	while (fNext != this) {
		DelayQueueEntry* entryToRemove = fNext;
		removeEntry(entryToRemove);
		delete entryToRemove;
	}
}

void DelayQueue::addEntry(DelayQueueEntry* newEntry) {
	//更新队列的时间信息
	synchronize();

	DelayQueueEntry* cur = head();
	while (newEntry->fDeltaTimeRemaining >= cur->fDeltaTimeRemaining) {
		newEntry->fDeltaTimeRemaining -= cur->fDeltaTimeRemaining;
		cur = cur->fNext;
	}

	cur->fDeltaTimeRemaining -= newEntry->fDeltaTimeRemaining;

	// Add "newEntry" to the queue, just before "cur":
	newEntry->fNext = cur;
	newEntry->fPrev = cur->fPrev;
	cur->fPrev = newEntry->fPrev->fNext = newEntry;
}

void DelayQueue::updateEntry(DelayQueueEntry* entry, DelayInterval newDelay) {
	if (entry == NULL) return;

	removeEntry(entry);
	entry->fDeltaTimeRemaining = newDelay;
	addEntry(entry);
}

void DelayQueue::updateEntry(intptr_t tokenToFind, DelayInterval newDelay) {
	DelayQueueEntry* entry = findEntryByToken(tokenToFind);
	updateEntry(entry, newDelay);
}

void DelayQueue::removeEntry(DelayQueueEntry* entry) {
	if (entry == NULL || entry->fNext == NULL) return;

	entry->fNext->fDeltaTimeRemaining += entry->fDeltaTimeRemaining;
	entry->fPrev->fNext = entry->fNext;
	entry->fNext->fPrev = entry->fPrev;
	entry->fNext = entry->fPrev = NULL;
	// in case we should try to remove it again
}

DelayQueueEntry* DelayQueue::removeEntry(intptr_t tokenToFind) {
	DelayQueueEntry* entry = findEntryByToken(tokenToFind);
	removeEntry(entry);
	return entry;
}
//查询队头Entry（任务）还有多久到时
DelayInterval const& DelayQueue::timeToNextAlarm() {
	if (head()->fDeltaTimeRemaining == DELAY_ZERO) return DELAY_ZERO; // a common case
	//更新队列中的时间信息。
	synchronize();
	return head()->fDeltaTimeRemaining;
}
//
void DelayQueue::handleAlarm() {
	//如果队头的任务没有到时，更新队列时间信息
	if (head()->fDeltaTimeRemaining != DELAY_ZERO) synchronize();
	//如果队头任务到时间了，则从队列从删除，并调用handleTimeout方法。
	if (head()->fDeltaTimeRemaining == DELAY_ZERO) {
		// This event is due to be handled:
		DelayQueueEntry* toRemove = head();
		removeEntry(toRemove); // do this first, in case handler accesses queue
		//此处释放toRemove;
		toRemove->handleTimeout();
		
	}
}
//遍历整个队列，如果找到，则返回该Entry，否则返回NULL
DelayQueueEntry* DelayQueue::findEntryByToken(intptr_t tokenToFind) {
	DelayQueueEntry* cur = head();
	while (cur != this) {
		if (cur->token() == tokenToFind) return cur;
		cur = cur->fNext;
	}

	return NULL;
}

void DelayQueue::synchronize() {
	// First, figure out how much time has elapsed since the last sync:
	//首先，计算出当前时刻距上一次同步时刻的时间差
	EventTime timeNow = TimeNow();
	if (timeNow < fLastSyncTime) {
		// The system clock has apparently gone back in time; reset our sync time and return:
		//如果当前时刻小于上一次同步的时刻，说明系统时间向前调整了，那么此时需要重设一下同步时刻，然后返回。
		fLastSyncTime = timeNow;
		return;
	}
	//计算出的时间差。
	DelayInterval timeSinceLastSync = timeNow - fLastSyncTime;
	fLastSyncTime = timeNow;

	// Then, adjust the delay queue for any entries whose time is up:
	//然后，调整那些已经到时间了的Entry
	DelayQueueEntry* curEntry = head();
	while (timeSinceLastSync >= curEntry->fDeltaTimeRemaining) {
		timeSinceLastSync -= curEntry->fDeltaTimeRemaining;
		curEntry->fDeltaTimeRemaining = DELAY_ZERO;
		curEntry = curEntry->fNext;
	}
	curEntry->fDeltaTimeRemaining -= timeSinceLastSync;
}


///// EventTime /////
//获取当前时间，gettimeofday可以精确到毫秒（ms），并且该函数不是系统调用
EventTime TimeNow() {
	struct timeval tvNow;
	
	gettimeofday(&tvNow, NULL);

	return EventTime(tvNow.tv_sec, tvNow.tv_usec);
}

const EventTime THE_END_OF_TIME(INT_MAX);
