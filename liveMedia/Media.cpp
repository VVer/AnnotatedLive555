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
// Media
// Implementation

#include "Media.hh"
#include "HashTable.hh"

////////// Medium //////////

Medium::Medium(UsageEnvironment& env)
: fEnviron(env), fNextTask(NULL) {
	// First generate a name for the new medium:
	//���ȣ�����ý����Դ������
	MediaLookupTable::ourMedia(env)->generateNewName(fMediumName, mediumNameMaxLen);
	env.setResultMsg(fMediumName);

	// Then add it to our table:
	//�����л�����һ����ϣ������ӱ�ý����Ϣ
	MediaLookupTable::ourMedia(env)->addNew(this, fMediumName);
}

Medium::~Medium() {
	// Remove any tasks that might be pending for us:
	fEnviron.taskScheduler().unscheduleDelayedTask(fNextTask);
}

Boolean Medium::lookupByName(UsageEnvironment& env, char const* mediumName,
	Medium*& resultMedium) {
	resultMedium = MediaLookupTable::ourMedia(env)->lookup(mediumName);
	if (resultMedium == NULL) {
		env.setResultMsg("Medium ", mediumName, " does not exist");
		return False;
	}
	return True;
}

void Medium::close(UsageEnvironment& env, char const* name) {
	MediaLookupTable::ourMedia(env)->remove(name);
}

void Medium::close(Medium* medium) {
	if (medium == NULL) return;

	close(medium->envir(), medium->name());
}

Boolean Medium::isSource() const {
	return False; // default implementation
}

Boolean Medium::isSink() const {
	return False; // default implementation
}

Boolean Medium::isRTCPInstance() const {
	return False; // default implementation
}

Boolean Medium::isRTSPClient() const {
	return False; // default implementation
}

Boolean Medium::isRTSPServer() const {
	return False; // default implementation
}

Boolean Medium::isMediaSession() const {
	return False; // default implementation
}

Boolean Medium::isServerMediaSession() const {
	return False; // default implementation
}

Boolean Medium::isDarwinInjector() const {
	return False; // default implementation
}


////////// _Tables implementation //////////
//liveMediaPriv��һ����ϣ���˴�ʵ����һ������ģʽ
_Tables* _Tables::getOurTables(UsageEnvironment& env, Boolean createIfNotPresent) {
	//���liveMediaPrivΪ�գ��򴴽�һ����ϣ������liveMediaPrivָ���½��Ĺ�ϣ��
	// Ȼ�󷵻ش˹�ϣ��
	if (env.liveMediaPriv == NULL && createIfNotPresent) {
		env.liveMediaPriv = new _Tables(env);
	}
	return (_Tables*)(env.liveMediaPriv);
}

void _Tables::reclaimIfPossible() {
	//��� mediaTable��socketTable��Ϊ�յĻ�����ɾ�������󣬻�����Դ
	if (mediaTable == NULL && socketTable == NULL) {
		fEnv.liveMediaPriv = NULL;
		delete this;
	}
}
 //_Table��Ĺ��캯��
_Tables::_Tables(UsageEnvironment& env)
: mediaTable(NULL), socketTable(NULL), fEnv(env) {
}

_Tables::~_Tables() {
}


////////// MediaLookupTable implementation //////////

MediaLookupTable* MediaLookupTable::ourMedia(UsageEnvironment& env) {

	_Tables* ourTables = _Tables::getOurTables(env);
	if (ourTables->mediaTable == NULL) {
		// Create a new table to record the media that are to be created in
		// this environment:
		//����һ�����б�������Ҫ��һ����ϣ�����ý����Ϣ
		ourTables->mediaTable = new MediaLookupTable(env);
	}
	return ourTables->mediaTable;
}
//�ӹ�ϣ���в�����ô��Ӧ��Medium
Medium* MediaLookupTable::lookup(char const* name) const {
	return (Medium*)(fTable->Lookup(name));
}

//��mediumName/Medium��ӵ���ϣ��fTable��
void MediaLookupTable::addNew(Medium* medium, char* mediumName) {
	fTable->Add(mediumName, (void*)medium);
}

//��ý����ϢMedium�ȴӹ�ϣ��fTable��ɾ����Ȼ���ͷ�Medium
void MediaLookupTable::remove(char const* name) {
	//�ȴӹ�ϣ���в���name��Ӧ��medium
	//���������ڣ���ɾ���ö���
	Medium* medium = lookup(name);
	if (medium != NULL) {
		fTable->Remove(name);
		if (fTable->IsEmpty()) {
			// We can also delete ourselves (to reclaim space):
			//ɾ���Լ����ڻ��տռ�
			_Tables* ourTables = _Tables::getOurTables(fEnv);
			delete this;
			ourTables->mediaTable = NULL;
			ourTables->reclaimIfPossible();
		}
		//�ͷ�medium
		delete medium;
	}
}

void MediaLookupTable::generateNewName(char* mediumName,
	unsigned /*maxLen*/) {
	// We should really use snprintf() here, but not all systems have it
	//ý����������liveMediaN
	sprintf(mediumName, "liveMedia%d", fNameGenerator++);
}

MediaLookupTable::MediaLookupTable(UsageEnvironment& env)
: fEnv(env), fTable(HashTable::create(STRING_HASH_KEYS)), fNameGenerator(0) {
}

MediaLookupTable::~MediaLookupTable() {
	//ɾ���ڶ���������ڴ�
	delete fTable;
}
