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
// "liveMedia"
// Copyright (c) 1996-2014 Live Networks, Inc.  All rights reserved.
// A data structure that represents a session that consists of
// potentially multiple (audio and/or video) sub-sessions
// (This data structure is used for media *streamers* - i.e., servers.
//  For media receivers, use "MediaSession" instead.)
// Implementation

#include "ServerMediaSession.hh"
#include <GroupsockHelper.hh>
#include <math.h>
#if defined(__WIN32__) || defined(_WIN32) || defined(_QNX4)
#define snprintf _snprintf
#endif

////////// ServerMediaSession //////////

ServerMediaSession* ServerMediaSession
::createNew(UsageEnvironment& env,
char const* streamName, char const* info,
char const* description, Boolean isSSM, char const* miscSDPLines) {
	return new ServerMediaSession(env, streamName, info, description,
		isSSM, miscSDPLines);
}

Boolean ServerMediaSession
::lookupByName(UsageEnvironment& env, char const* mediumName,
ServerMediaSession*& resultSession) {
	resultSession = NULL; // unless we succeed

	Medium* medium;
	if (!Medium::lookupByName(env, mediumName, medium)) return False;

	if (!medium->isServerMediaSession()) {
		env.setResultMsg(mediumName, " is not a 'ServerMediaSession' object");
		return False;
	}

	resultSession = (ServerMediaSession*)medium;
	return True;
}

static char const* const libNameStr = "LIVE555 Streaming Media v";
char const* const libVersionStr = LIVEMEDIA_LIBRARY_VERSION_STRING;
//
//
ServerMediaSession::ServerMediaSession(UsageEnvironment& env,
	char const* streamName,
	char const* info,
	char const* description,
	Boolean isSSM, char const* miscSDPLines)
	: Medium(env), fIsSSM(isSSM), fSubsessionsHead(NULL)/*ServerMediaSubsession是一个单向链表，fSubsessionsHead指向该表的头部*/,
	fSubsessionsTail(NULL), fSubsessionCounter(0)/*ServerMediaSubsession的个数*/,
	fReferenceCount(0), fDeleteWhenUnreferenced(False) {
	fStreamName = strDup(streamName == NULL ? "" : streamName);

	char* libNamePlusVersionStr = NULL; // by default
	//如果info或者description为空，则生成libNamePlusVersionStr信息
	if (info == NULL || description == NULL) {
		libNamePlusVersionStr = new char[strlen(libNameStr) + strlen(libVersionStr) + 1];
		sprintf(libNamePlusVersionStr, "%s%s", libNameStr, libVersionStr);
	}

	fInfoSDPString = strDup(info == NULL ? libNamePlusVersionStr : info);
	fDescriptionSDPString = strDup(description == NULL ? libNamePlusVersionStr : description);
	delete[] libNamePlusVersionStr;

	fMiscSDPLines = strDup(miscSDPLines == NULL ? "" : miscSDPLines);

	gettimeofday(&fCreationTime, NULL);
}

ServerMediaSession::~ServerMediaSession() {
	deleteAllSubsessions();
	delete[] fStreamName;
	delete[] fInfoSDPString;
	delete[] fDescriptionSDPString;
	delete[] fMiscSDPLines;
}

Boolean
ServerMediaSession::addSubsession(ServerMediaSubsession* subsession) {
	if (subsession->fParentSession != NULL) return False; // it's already used 此subsession的fParentSession指针已经指向了另外一个ServerMediaSession

	//第一次添加的时候，Tail和Head均为空，所以Tail和Head均指向次subsession
	//以后添加都是在subsession的后面添加
	// fTrackNumber1--->fTrackNumber2--->fTrackNumber3
	if (fSubsessionsTail == NULL) {
		fSubsessionsHead = subsession;
	}
	else {
		fSubsessionsTail->fNext = subsession;
	}
	fSubsessionsTail = subsession;
	//sbs的fParentSession指向当前的ServerMediaSession
	subsession->fParentSession = this;
	subsession->fTrackNumber = ++fSubsessionCounter;
	return True;
}

void ServerMediaSession::testScaleFactor(float& scale) {
	// First, try setting all subsessions to the desired scale.
	// If the subsessions' actual scales differ from each other, choose the
	// value that's closest to 1, and then try re-setting all subsessions to that
	// value.  If the subsessions' actual scales still differ, re-set them all to 1.
	float minSSScale = 1.0;
	float maxSSScale = 1.0;
	float bestSSScale = 1.0;
	float bestDistanceTo1 = 0.0;
	ServerMediaSubsession* subsession;
	for (subsession = fSubsessionsHead; subsession != NULL;
		subsession = subsession->fNext) {
		float ssscale = scale;
		//目前scale支持1
		/*参见
		void ServerMediaSubsession::testScaleFactor(float& scale) {
		// default implementation: Support scale = 1 only
		scale = 1;
		}*/
		subsession->testScaleFactor(ssscale);
		if (subsession == fSubsessionsHead) { // this is the first subsession
			minSSScale = maxSSScale = bestSSScale = ssscale;
			bestDistanceTo1 = (float)fabs(ssscale - 1.0f);
		}
		else {
			if (ssscale < minSSScale) {
				minSSScale = ssscale;
			}
			else if (ssscale > maxSSScale) {
				maxSSScale = ssscale;
			}

			float distanceTo1 = (float)fabs(ssscale - 1.0f);
			if (distanceTo1 < bestDistanceTo1) {
				bestSSScale = ssscale;
				bestDistanceTo1 = distanceTo1;
			}
		}
	}
	if (minSSScale == maxSSScale) {
		// All subsessions are at the same scale: minSSScale == bestSSScale == maxSSScale
		scale = minSSScale;
		return;
	}

	// The scales for each subsession differ.  Try to set each one to the value
	// that's closest to 1:
	for (subsession = fSubsessionsHead; subsession != NULL;
		subsession = subsession->fNext) {
		float ssscale = bestSSScale;
		subsession->testScaleFactor(ssscale);
		if (ssscale != bestSSScale) break; // no luck
	}
	if (subsession == NULL) {
		// All subsessions are at the same scale: bestSSScale
		scale = bestSSScale;
		return;
	}

	// Still no luck.  Set each subsession's scale to 1:
	for (subsession = fSubsessionsHead; subsession != NULL;
		subsession = subsession->fNext) {
		float ssscale = 1;
		subsession->testScaleFactor(ssscale);
	}
	//整体的scale目前也是1
	scale = 1;
}

float ServerMediaSession::duration() const {
	float minSubsessionDuration = 0.0;
	float maxSubsessionDuration = 0.0;
	for (ServerMediaSubsession* subsession = fSubsessionsHead; subsession != NULL;
		subsession = subsession->fNext) {
		// Hack: If any subsession supports seeking by 'absolute' time, then return a negative value, to indicate that only subsessions
		// will have a "a=range:" attribute:
		char* absStartTime = NULL; char* absEndTime = NULL;
		subsession->getAbsoluteTimeRange(absStartTime, absEndTime);
		if (absStartTime != NULL) return -1.0f;

		//将其他sbs的时长和第一个sbs时长保持一致。
		float ssduration = subsession->duration(); //返回值是0
		if (subsession == fSubsessionsHead) { // this is the first subsession
			minSubsessionDuration = maxSubsessionDuration = ssduration;
		}
		else if (ssduration < minSubsessionDuration) {
			minSubsessionDuration = ssduration;
		}
		else if (ssduration > maxSubsessionDuration) {
			maxSubsessionDuration = ssduration;
		}
	}

	if (maxSubsessionDuration != minSubsessionDuration) {
		return -maxSubsessionDuration; // because subsession durations differ
	}
	else {
		//默认情况下，max和min均为0.0
		return maxSubsessionDuration; // all subsession durations are the same
	}
}

//目测这里面没有迭代删除所有的Subsessions
//尝试删除所有的SeverMediaSession
//
void ServerMediaSession::deleteAllSubsessions() {

	Medium::close(fSubsessionsHead); //fSubsessionsHead的析构函数会逐个调用Medium::close来析构所有元素。
	fSubsessionsHead = fSubsessionsTail = NULL;
	fSubsessionCounter = 0;
}

Boolean ServerMediaSession::isServerMediaSession() const {
	return True;
}

char* ServerMediaSession::generateSDPDescription() {
	AddressString ipAddressStr(ourIPAddress(envir()));
	unsigned ipAddressStrSize = strlen(ipAddressStr.val());

	// For a SSM sessions, we need a "a=source-filter: incl ..." line also:
	char* sourceFilterLine;
	if (fIsSSM) {
		char const* const sourceFilterFmt =
			"a=source-filter: incl IN IP4 * %s\r\n"
			"a=rtcp-unicast: reflection\r\n";
		unsigned const sourceFilterFmtSize = strlen(sourceFilterFmt) + ipAddressStrSize + 1;

		sourceFilterLine = new char[sourceFilterFmtSize];
		sprintf(sourceFilterLine, sourceFilterFmt, ipAddressStr.val());
	}
	else {
		sourceFilterLine = strDup("");
	}

	char* rangeLine = NULL; // for now
	char* sdp = NULL; // for now

	do {
		// Count the lengths of each subsession's media-level SDP lines.
		// (We do this first, because the call to "subsession->sdpLines()"
		// causes correct subsession 'duration()'s to be calculated later.)
		unsigned sdpLength = 0;
		ServerMediaSubsession* subsession;
		for (subsession = fSubsessionsHead; subsession != NULL;
			subsession = subsession->fNext) {
			char const* sdpLines = subsession->sdpLines();
			if (sdpLines == NULL) continue; // the media's not available
			sdpLength += strlen(sdpLines);
		}
		if (sdpLength == 0) break; // the session has no usable subsessions

		// Unless subsessions have differing durations, we also have a "a=range:" line:
		float dur = duration();
		if (dur == 0.0) {
			rangeLine = strDup("a=range:npt=0-\r\n");
		}
		else if (dur > 0.0) {
			char buf[100];
			sprintf(buf, "a=range:npt=0-%.3f\r\n", dur);
			rangeLine = strDup(buf);
		}
		else { // subsessions have differing durations, so "a=range:" lines go there
			rangeLine = strDup("");
		}

		char const* const sdpPrefixFmt =
			"v=0\r\n"
			"o=- %ld%06ld %d IN IP4 %s\r\n"
			"s=%s\r\n"
			"i=%s\r\n"
			"t=0 0\r\n"
			"a=tool:%s%s\r\n"
			"a=type:broadcast\r\n"
			"a=control:*\r\n"
			"%s"
			"%s"
			"a=x-qt-text-nam:%s\r\n"
			"a=x-qt-text-inf:%s\r\n"
			"%s";
		sdpLength += strlen(sdpPrefixFmt)
			+ 20 + 6 + 20 + ipAddressStrSize
			+ strlen(fDescriptionSDPString)
			+ strlen(fInfoSDPString)
			+ strlen(libNameStr) + strlen(libVersionStr)
			+ strlen(sourceFilterLine)
			+ strlen(rangeLine)
			+ strlen(fDescriptionSDPString)
			+ strlen(fInfoSDPString)
			+ strlen(fMiscSDPLines);
		sdpLength += 1000; // in case the length of the "subsession->sdpLines()" calls below change
		sdp = new char[sdpLength];
		if (sdp == NULL) break;

		// Generate the SDP prefix (session-level lines):
		snprintf(sdp, sdpLength, sdpPrefixFmt,
			fCreationTime.tv_sec, fCreationTime.tv_usec, // o= <session id>
			1, // o= <version> // (needs to change if params are modified)
			ipAddressStr.val(), // o= <address>
			fDescriptionSDPString, // s= <description>
			fInfoSDPString, // i= <info>
			libNameStr, libVersionStr, // a=tool:
			sourceFilterLine, // a=source-filter: incl (if a SSM session)
			rangeLine, // a=range: line
			fDescriptionSDPString, // a=x-qt-text-nam: line
			fInfoSDPString, // a=x-qt-text-inf: line
			fMiscSDPLines); // miscellaneous session SDP lines (if any)

		// Then, add the (media-level) lines for each subsession:
		char* mediaSDP = sdp;
		for (subsession = fSubsessionsHead; subsession != NULL;
			subsession = subsession->fNext) {
			unsigned mediaSDPLength = strlen(mediaSDP);
			mediaSDP += mediaSDPLength;
			sdpLength -= mediaSDPLength;
			if (sdpLength <= 1) break; // the SDP has somehow become too long

			char const* sdpLines = subsession->sdpLines();
			if (sdpLines != NULL) snprintf(mediaSDP, sdpLength, "%s", sdpLines);
		}
	} while (0);

	delete[] rangeLine; delete[] sourceFilterLine;
	return sdp;
}


////////// ServerMediaSessionIterator //////////

ServerMediaSubsessionIterator
::ServerMediaSubsessionIterator(ServerMediaSession& session)
: fOurSession(session) {
	reset();
}

ServerMediaSubsessionIterator::~ServerMediaSubsessionIterator() {
}

//返回当前节点，并将指针移到下一个节点
ServerMediaSubsession* ServerMediaSubsessionIterator::next() {
	ServerMediaSubsession* result = fNextPtr;
	if (fNextPtr != NULL) fNextPtr = fNextPtr->fNext;

	return result;
}

void ServerMediaSubsessionIterator::reset() {
	//将fNextPtr指向fOurSession.fSubsessionsHead，也就是fOurSession的第一个fSubsessionsHead
	//
	fNextPtr = fOurSession.fSubsessionsHead;
}


////////// ServerMediaSubsession //////////

ServerMediaSubsession::ServerMediaSubsession(UsageEnvironment& env)
: Medium(env),
fParentSession(NULL), fServerAddressForSDP(0), fPortNumForSDP(0),
fNext(NULL), fTrackNumber(0), fTrackId(NULL) {
}

ServerMediaSubsession::~ServerMediaSubsession() {
	delete[](char*)fTrackId;
	Medium::close(fNext);
}

char const* ServerMediaSubsession::trackId() {
	if (fTrackNumber == 0) return NULL; // not yet in a ServerMediaSession

	if (fTrackId == NULL) {
		char buf[100];
		sprintf(buf, "track%d", fTrackNumber);
		fTrackId = strDup(buf);
	}
	return fTrackId;
}

void ServerMediaSubsession::pauseStream(unsigned /*clientSessionId*/,
	void* /*streamToken*/) {
	// default implementation: do nothing
}
void ServerMediaSubsession::seekStream(unsigned /*clientSessionId*/,
	void* /*streamToken*/, double& /*seekNPT*/, double /*streamDuration*/, u_int64_t& numBytes) {
	// default implementation: do nothing
	numBytes = 0;
}
void ServerMediaSubsession::seekStream(unsigned /*clientSessionId*/,
	void* /*streamToken*/, char*& absStart, char*& absEnd) {
	// default implementation: do nothing (but delete[] and assign "absStart" and "absEnd" to NULL, to show that we don't handle this)
	delete[] absStart; absStart = NULL;
	delete[] absEnd; absEnd = NULL;
}
void ServerMediaSubsession::nullSeekStream(unsigned /*clientSessionId*/, void* /*streamToken*/,
	double streamEndTime, u_int64_t& numBytes) {
	// default implementation: do nothing
	numBytes = 0;
}
void ServerMediaSubsession::setStreamScale(unsigned /*clientSessionId*/,
	void* /*streamToken*/, float /*scale*/) {
	// default implementation: do nothing
}
float ServerMediaSubsession::getCurrentNPT(void* /*streamToken*/) {
	// default implementation: return 0.0
	return 0.0;
}
FramedSource* ServerMediaSubsession::getStreamSource(void* /*streamToken*/) {
	// default implementation: return NULL
	return NULL;
}
void ServerMediaSubsession::deleteStream(unsigned /*clientSessionId*/,
	void*& /*streamToken*/) {
	// default implementation: do nothing
}

void ServerMediaSubsession::testScaleFactor(float& scale) {
	// default implementation: Support scale = 1 only
	scale = 1;
}

float ServerMediaSubsession::duration() const {
	// default implementation: assume an unbounded session:
	//默认情况下，假定是一个不限制时长的会话
	return 0.0;
}

void ServerMediaSubsession::getAbsoluteTimeRange(char*& absStartTime, char*& absEndTime) const {
	// default implementation: We don't support seeking by 'absolute' time, so indicate this by setting both parameters to NULL:
	//默认情况下，live555不支持绝对时间，所以将两个参数都设置为NULL，表示不支持。
	absStartTime = absEndTime = NULL;
}
//参数addressBits： 主机IP地址的无符号整型（32位）
//参数 portBits：端口号，short类型
void ServerMediaSubsession::setServerAddressAndPortForSDP(netAddressBits addressBits,
	portNumBits portBits) {
	fServerAddressForSDP = addressBits;
	fPortNumForSDP = portBits;
}

char const*
ServerMediaSubsession::rangeSDPLine() const {
	// First, check for the special case where we support seeking by 'absolute' time:
	char* absStart = NULL; char* absEnd = NULL;
	getAbsoluteTimeRange(absStart, absEnd);
	if (absStart != NULL) {
		char buf[100];

		if (absEnd != NULL) {
			sprintf(buf, "a=range:clock=%s-%s\r\n", absStart, absEnd);
		}
		else {
			sprintf(buf, "a=range:clock=%s-\r\n", absStart);
		}
		return strDup(buf);
	}

	if (fParentSession == NULL) return NULL;

	// If all of our parent's subsessions have the same duration
	// (as indicated by "fParentSession->duration() >= 0"), there's no "a=range:" line:
	if (fParentSession->duration() >= 0.0) return strDup("");

	// Use our own duration for a "a=range:" line:
	float ourDuration = duration();
	if (ourDuration == 0.0) {
		return strDup("a=range:npt=0-\r\n");
	}
	else {
		char buf[100];
		sprintf(buf, "a=range:npt=0-%.3f\r\n", ourDuration);
		return strDup(buf);
	}
}
