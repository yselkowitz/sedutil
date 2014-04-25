/* C:B**************************************************************************
This software is Copyright 2014 Michael Romeo <r0m30@r0m30.com>

This file is part of msed.

msed is free software: you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation, either version 3 of the License, or
(at your option) any later version.

msed is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with msed.  If not, see <http://www.gnu.org/licenses/>.

 * C:E********************************************************************** */
#include "os.h"
#include <stdio.h>
#include <iostream>
#include <iomanip>
#include "MsedDev.h"
#include "MsedHexDump.h"
#include "MsedCommand.h"
#include "MsedSession.h"
#include "MsedEndianFixup.h"
#include "MsedStructures.h"
#include "MsedResponse.h"
#include "MsedTasks.h"
#include "MsedHashPwd.h"

using namespace std;

int diskQuery(char * devref)
{
    LOG(D4) << "Entering diskQuery()" << devref;
    MsedDev * dev = new MsedDev(devref);
    if (!dev->isPresent()) {
        LOG(E) << "Device not present " << devref;
        delete dev;
        return 1;
    }
    if (!dev->isOpal2()) {
        LOG(E) << "Device not Opal2 " << devref;
        delete dev;
        return 1;
    }
    dev->puke();
    delete dev;
    return 0;
}

int diskScan()
{
    char devname[25];
    int i = 0;
    uint8_t FirmwareRev[8];
    uint8_t ModelNum[40];
    MsedDev * d;
    LOG(D4) << "Creating diskList";
    printf("\nScanning for Opal 2.0 compliant disks\n");
    while (TRUE) {
        SNPRINTF(devname, 23, DEVICEMASK, i);
        //		sprintf_s(devname, 23, "\\\\.\\PhysicalDrive3", i);
        d = new MsedDev(devname);
        if (d->isPresent()) {
            d->getFirmwareRev(FirmwareRev);
            d->getModelNum(ModelNum);
            printf("%s %s", devname, (d->isOpal2() ? " Yes " : " No  "));
            for (int x = 0; x < sizeof (ModelNum); x++) {
                cout << ModelNum[x];
            }
            cout << " ";
            for (int x = 0; x < sizeof (FirmwareRev); x++) {
                //if (0x20 == FirmwareRev[x]) break;
                cout << FirmwareRev[x];
            }
            cout << std::endl;
            if (MAX_DISKS == i) {
                LOG(I) << MAX_DISKS << " disks, really?";
                delete d;
                return 1;
            }
        }
        else break;
        delete d;
        i += 1;
    }
    delete d;
    printf("No more disks present ending scan\n");
    return 0;
}

int takeOwnership(char * devref, char * newpassword)
{
    LOG(D4) << "Entering takeOwnership(char * devref, char * newpassword)";
    vector<uint8_t> hash, salt(DEFAULTSALT);
    MsedResponse response;
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    //	Start Session
    MsedSession * session = new MsedSession(device);
    if (session->start(OPAL_UID::OPAL_ADMINSP_UID)) {
        delete session;
        delete device;
        return 0xff;
    }
    // Get the default password
    // session[TSN:HSN] -> C_PIN_MSID_UID.Get[Cellblock : [startColumn = PIN,
    //                       endColumn = PIN]]
    vector<uint8_t> table;
    table.push_back(0xa8);
    for (int i = 0; i < 8; i++) {
        table.push_back(OPALUID[OPAL_UID::OPAL_C_PIN_MSID][i]);
    }
    if (getTable(session, table, 0x03, 0x03, response)) {
        delete session;
        delete device;
        return 0xff;
    }
    // session[TSN:HSN] <- EOS
    delete session;
    /*
     * We now have the PIN, sign on and take ownership
     */
    //	Start Session
    session = new MsedSession(device);
    session->dontHashPwd(); // this should not be hashed
    if (session->start(OPAL_UID::OPAL_ADMINSP_UID,
                       (char *) response.getString(4).c_str(),
                       OPAL_UID::OPAL_SID_UID)) {
        delete session;
        delete device;
        return 0xff;
    }
    // session[TSN:HSN] -> C_PIN_SID_UID.Set[Values = [PIN = <new_SID_password>]]
    table.clear();
    table.push_back(0xa8);
    for (int i = 0; i < 8; i++) {
        table.push_back(OPALUID[OPAL_UID::OPAL_C_PIN_SID][i]);
    }
    hash.clear();
    MsedHashPwd(hash, newpassword, salt);
    if (setTable(session, table, OPAL_TOKEN::PIN, hash)) {
        delete session;
        delete device;
        return 0xff;
    }
    LOG(I) << "takeownership complete new SID password = " << newpassword;
    // session[TSN:HSN] <- EOS
    delete session;
    delete device;
    LOG(D4) << "Exiting changeInitialPassword()";
    return 0;
}

int revertTPer(char * devref, char * password, uint8_t PSID)
{
    LOG(D4) << "Entering revertTPer(char * devref, char * password)";
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedCommand *cmd = new MsedCommand();
    MsedSession * session = new MsedSession(device);
    MsedResponse response;
    OPAL_UID uid = OPAL_UID::OPAL_SID_UID;
    if (PSID) {
        session->expectAbort(); // seems to immed abort on PSID auth fail
        session->dontHashPwd(); // PSID pwd should be passed as entered
        uid = OPAL_UID::OPAL_PSID_UID;
    }
    if (session->start(OPAL_UID::OPAL_ADMINSP_UID, password, uid)) {
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    //	session[TSN:HSN]->AdminSP_UID.Revert[]
    cmd->reset(OPAL_UID::OPAL_ADMINSP_UID, OPAL_METHOD::REVERT);
    cmd->addToken(OPAL_TOKEN::STARTLIST);
    cmd->addToken(OPAL_TOKEN::ENDLIST);
    cmd->complete();
    session->expectAbort();
    if (session->sendCommand(cmd, response)) {
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    LOG(I) << "revertTper completed successfully";
    delete cmd;
    delete session;
    delete device;
    LOG(D4) << "Exiting RevertTperevertTPer()";
    return 0;
}

int activateLockingSP(char * devref, char * password)
{
    LOG(D4) << "Entering activateLockingSP()";
    vector<uint8_t> table;
    table.push_back(0xa8);
    for (int i = 0; i < 8; i++) {
        table.push_back(OPALUID[OPAL_UID::OPAL_LOCKINGSP_UID][i]);
    }
    MsedResponse response;
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedCommand *cmd = new MsedCommand();
    MsedSession * session = new MsedSession(device);
    if (session->start(OPAL_UID::OPAL_ADMINSP_UID, password, OPAL_UID::OPAL_SID_UID)) {
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    //session[TSN:HSN]->LockingSP_UID.Get[Cellblock:[startColumn = LifeCycle,
    //                                               endColumn = LifeCycle]]
    if (getTable(session, table, 0x06, 0x06, response)) {
        LOG(E) << "Unable to determine LockingSP Lifecycle state";
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    // SL,SL,SN,NAME,Value
    //  0  1  2   3    4
    // verify response
    if ((0x06 != response.getUint8(3)) || // getlifecycle
        (0x08 != response.getUint8(4))) // Manufactured-Inactive
    {
        LOG(E) << "Locking SP lifecycle is not Manufactured-Inactive";
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    // session[TSN:HSN] -> LockingSP_UID.Activate[]
    cmd->reset(OPAL_UID::OPAL_LOCKINGSP_UID, OPAL_METHOD::ACTIVATE);
    cmd->addToken(OPAL_TOKEN::STARTLIST);
    cmd->addToken(OPAL_TOKEN::ENDLIST);
    cmd->complete();
    if (session->sendCommand(cmd, response)) {
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    // method response code 00
    LOG(I) << "Locking SP Activate Complete";

    delete cmd;
    delete session;
    delete device;
    LOG(D4) << "Exiting activatLockingSP()";
    return 0;
}

int revertLockingSP(char * devref, char * password, uint8_t keep)
{
    LOG(D4) << "Entering revert LockingSP() keep = " << keep;
    vector<uint8_t> keepgloballockingrange;
    keepgloballockingrange.push_back(0xa3);
    keepgloballockingrange.push_back(0x06);
    keepgloballockingrange.push_back(0x00);
    keepgloballockingrange.push_back(0x00);
    MsedResponse response;
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedCommand *cmd = new MsedCommand();
    MsedSession * session = new MsedSession(device);
    // session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
    //                   HostChallenge = <Admin1_password>, HostSigningAuthority = Admin1_UID]

    if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    // session[TSN:HSN]->ThisSP.RevertSP[]
    cmd->reset(OPAL_UID::OPAL_THISSP_UID, OPAL_METHOD::REVERTSP);
    cmd->addToken(OPAL_TOKEN::STARTLIST);
    if (keep) {
        cmd->addToken(OPAL_TOKEN::STARTNAME);
        cmd->addToken(keepgloballockingrange);
        cmd->addToken(OPAL_TINY_ATOM::UINT_01); // KeepGlobalRangeKey = TRUE
        cmd->addToken(OPAL_TOKEN::ENDNAME);
    }
    cmd->addToken(OPAL_TOKEN::ENDLIST);
    cmd->complete();
    if (session->sendCommand(cmd, response)) {
        delete cmd;
        delete session;
        delete device;
        return 0xff;
    }
    // empty list returned so rely on method status
    LOG(I) << "Revert LockingSP complete";
    session->expectAbort();
    delete session;
    delete device;
    LOG(D4) << "Exiting revert LockingSP()";
    return 0;
}

int setNewPassword(char * password, char * userid, char * newpassword, char * devref)
{
    LOG(D4) << "Entering setNewPassword" <<
            " Admin1 password = " << password << " user = " << userid <<
            " newpassword = " << newpassword << " device = " << devref;

    std::vector<uint8_t> userCPIN, hash, salt(DEFAULTSALT);
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedSession * session = new MsedSession(device);
    // session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
    //               HostChallenge = <new_SID_password>, HostSigningAuthority = Admin1_UID]
    if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
        delete session;
        delete device;
        return 0xff;
    }
    // Get C_PIN ID  for user from Authority table
    if (getAuth4User(userid, 10, userCPIN)) {
        LOG(E) << "Unable to find user " << userid << " in Authority Table";
        delete session;
        delete device;
        return 0xff;
    }
    // session[TSN:HSN] -> C_PIN_user_UID.Set[Values = [PIN = <new_password>]]
    MsedHashPwd(hash, newpassword, salt);
    if (setTable(session, userCPIN, OPAL_TOKEN::PIN, hash)) {
        LOG(E) << "Unable to set user " << userid << " new password ";
        delete session;
        delete device;
        return 0xff;
    }
    LOG(I) << userid << " password changed to " << newpassword;
    // session[TSN:HSN] <- EOS
    delete session;
    delete device;
    LOG(D4) << "Exiting setNewPassword()";
    return 0;
}

int enableUser(char * password, char * userid, char * devref)
{
    LOG(D4) << "Enable User()" <<
            " Admin1 password = " << password << " user = " << userid << " on" << devref;
    /*
     * Enable a user in the lockingSP
     */
    vector<uint8_t> userUID, opalTRUE;
    opalTRUE.push_back(0x01);
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedSession * session = new MsedSession(device);
    // session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
    //               HostChallenge = <new_SID_password>, HostSigningAuthority = Admin1_UID]
    if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
        delete session;
        delete device;
        return 0xff;
    }
    // Get UID for user from Authority table
    if (getAuth4User(userid, 0, userUID)) {
        LOG(E) << "Unable to find user " << userid << " in Authority Table";
        delete session;
        delete device;
        return 0xff;
    }
    // session[TSN:HSN] -> User1_UID.Set[Values = [Enabled = TRUE]]
    if (setTable(session, userUID, (OPAL_TOKEN) 0x05, opalTRUE)) {
        LOG(E) << "Unable to enable user " << userid;
        delete session;
        delete device;
        return 0xff;
    }
    LOG(I) << userid << " has been enabled ";
    // session[TSN:HSN] <- EOS
    delete session;
    delete device;
    LOG(D4) << "Exiting enable user()";
    return 0;
}
int getAuth4User(char * userid, uint8_t uidorcpin, std::vector<uint8_t> &userData) {
	uint8_t uidnum;
	userData.clear();
	userData.push_back(0xa8);
	userData.push_back(0x00);
	userData.push_back(0x00);
	userData.push_back(0x00);
	switch (uidorcpin) {
	case 0:
		userData.push_back(0x09);
		break;
	case 10:
		userData.push_back(0x0b);
		break;
	default:
		LOG(E) << "Invalid Userid data requested" << (uint16_t)uidorcpin;
		return 0xff;
	}


	switch (strnlen(userid, 20)) {
	case 5:
		if (memcmp("User", userid, 4)) {
			LOG(E) << "Invalid Userid " << userid;
			return 0xff;
		}
		else {
			uidnum = userid[4] & 0x0f;
			userData.push_back(0x00);
			userData.push_back(0x03);
			userData.push_back(0x00);
			userData.push_back(uidnum);
		}
		break;
	case 6:
		if (memcmp("Admin", userid, 5)) {
			LOG(E) << "Invalid Userid " << userid;
			return 0xff;
		}
		else {
			uidnum = userid[5] & 0x0f;
			userData.push_back(0x00);
			userData.push_back(0x01);
			userData.push_back(0x00);
			userData.push_back(uidnum);
		}
		break;
	default:
		LOG(E) << "Invalid Userid " << userid;
		return 0xff;
	}
	if (0 == uidnum) {
		LOG(E) << "Invalid Userid " << userid;
		userData.clear();
		return 0xff;
	}
	return 0;
}
// Samsung EVO 840 will not return userids from authority table (bug??)
//int getAuth4User(char * userid, uint8_t column, std::vector<uint8_t> &userData, MsedSession * session)
//{
//    LOG(D4) << "Entering getAuth4User()";
//    std::vector<uint8_t> table, key, nextkey;
//    MsedResponse response;
//    // build a token for the authority table
//    table.push_back(0xa8);
//    for (int i = 0; i < 8; i++) {
//        table.push_back(OPALUID[OPAL_UID::OPAL_AUTHORITY_TABLE][i]);
//    }
//    key.clear();
//    while (true) {
//        // Get the next UID
//        if (nextTable(session, table, key, response)) {
//            return 0xff;
//        }
//        key = response.getRawToken(2);
//        nextkey = response.getRawToken(3);
//
//        //AUTHORITY_TABLE.Get[Cellblock : [startColumn = 0, endColumn = 10]]
//        if (getTable(session, key, 1, 1, response)) {
//            return 0xff;
//        }
//        if (!(strcmp(userid, response.getString(4).c_str()))) {
//            if (getTable(session, key, column, column, response)) {
//                return 0xff;
//            }
//            userData = response.getRawToken(4);
//            return 0x00;
//        }
//
//        if (9 != nextkey.size()) break; // no next row so bail
//        key = nextkey;
//    }
//    return 0xff;
//}

int dumpTable(char * password, char * devref)
{
    CLog::Level() = CLog::FromInt(4);
    LOG(D4) << "Entering getTable()";
    vector<uint8_t> table, key, nextkey, temp;
    table.push_back(0xa8);
    for (int i = 0; i < 8; i++) {
        table.push_back(OPALUID[OPAL_UID::OPAL_AUTHORITY_TABLE][i]);
    }
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedSession * session = new MsedSession(device);
    MsedResponse response;
    // session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
    //                   HostChallenge = <Admin1_password>, HostSigningAuthority = Admin1_UID]
    if (session->start(OPAL_UID::OPAL_ADMINSP_UID, password, OPAL_UID::OPAL_SID_UID)) {
        delete session;
        delete device;
        return 0xff;
    }

    key.clear();
    while (true) {
        // Get the next UID
        if (nextTable(session, table, key, response)) {
            delete session;
            delete device;
            return 0xff;
        }
        key = response.getRawToken(2);
        nextkey = response.getRawToken(3);

        //AUTHORITY_TABLE.Get[Cellblock : [startColumn = 0, endColumn = 10]]

        if (getTable(session, key, 0, 4, response)) {
            delete session;
            delete device;
            return 0xff;
        }
		uint32_t i = 0;
		while(i < response.getTokenCount()) {
			if (OPAL_TOKEN::STARTNAME == response.tokenIs(i)) {
				LOG(I) << "col " << (uint32_t)response.getUint32(i + 1);
				temp = response.getRawToken(i + 2);
				MsedHexDump(temp.data(), temp.size());
				i += 2;
			}
			i++;
		} 

        if (9 != nextkey.size()) break; // no next row so bail
        key = nextkey;
    }
    delete session;
    delete device;
    return 0;
}

int nextTable(MsedSession * session, std::vector<uint8_t> table,
              std::vector<uint8_t> startkey, MsedResponse & response)
{
    LOG(D4) << "Entering nextTable";
    MsedCommand *next = new MsedCommand();
    next->reset(OPAL_UID::OPAL_AUTHORITY_TABLE, OPAL_METHOD::NEXT);
    next->changeInvokingUid(table);
    next->addToken(OPAL_TOKEN::STARTLIST);
    if (0 != startkey.size()) {
        next->addToken(OPAL_TOKEN::STARTNAME);
        next->addToken(OPAL_TINY_ATOM::UINT_00);
        //startkey[0] = 0x00;
        next->addToken(startkey);
        next->addToken(OPAL_TOKEN::ENDNAME);
    }
    next->addToken(OPAL_TOKEN::STARTNAME);
    next->addToken(OPAL_TINY_ATOM::UINT_01);
    next->addToken(OPAL_TINY_ATOM::UINT_02);
    next->addToken(OPAL_TOKEN::ENDNAME);
    next->addToken(OPAL_TOKEN::ENDLIST);
    next->complete();
    if (session->sendCommand(next, response)) {
        delete next;
        return 0xff;
    }
    delete next;
    return 0;
}

int getTable(MsedSession * session, vector<uint8_t> table,
             uint16_t startcol, uint16_t endcol, MsedResponse & response)
{
    LOG(D4) << "Entering getTable";
    MsedCommand *get = new MsedCommand();
    get->reset(OPAL_UID::OPAL_AUTHORITY_TABLE, OPAL_METHOD::GET);
    get->changeInvokingUid(table);
    get->addToken(OPAL_TOKEN::STARTLIST);
    get->addToken(OPAL_TOKEN::STARTLIST);
    get->addToken(OPAL_TOKEN::STARTNAME);
    get->addToken(OPAL_TOKEN::STARTCOLUMN);
    get->addToken(startcol);
    get->addToken(OPAL_TOKEN::ENDNAME);
    get->addToken(OPAL_TOKEN::STARTNAME);
    get->addToken(OPAL_TOKEN::ENDCOLUMN);
    get->addToken(endcol);
    get->addToken(OPAL_TOKEN::ENDNAME);
    get->addToken(OPAL_TOKEN::ENDLIST);
    get->addToken(OPAL_TOKEN::ENDLIST);
    get->complete();
    if (session->sendCommand(get, response)) {
        delete get;
        return 0xff;
    }
    delete get;
    return 0;
}

int setTable(MsedSession * session, vector<uint8_t> table,
             OPAL_TOKEN name, vector<uint8_t> value)
{
    LOG(D4) << "Entering setTable";
    MsedResponse response;
    MsedCommand *set = new MsedCommand();
    set->reset(OPAL_UID::OPAL_AUTHORITY_TABLE, OPAL_METHOD::SET);
    set->changeInvokingUid(table);
    set->addToken(OPAL_TOKEN::STARTLIST);
    set->addToken(OPAL_TOKEN::STARTNAME);
    set->addToken(OPAL_TOKEN::VALUES); // "values"
    set->addToken(OPAL_TOKEN::STARTLIST);
    set->addToken(OPAL_TOKEN::STARTNAME);
    set->addToken(name);
    set->addToken(value);
    set->addToken(OPAL_TOKEN::ENDNAME);
    set->addToken(OPAL_TOKEN::ENDLIST);
    set->addToken(OPAL_TOKEN::ENDNAME);
    set->addToken(OPAL_TOKEN::ENDLIST);
    set->complete();
    if (session->sendCommand(set, response)) {
        LOG(E) << "Set Failed ";
        delete set;
        return 0xff;
    }
    delete set;
    LOG(D4) << "Leaving setTable";
    return 0;
}

int MsedSetLSP(OPAL_UID table_uid, OPAL_TOKEN name, vector<uint8_t> value,
               char * password, char * devref)
{
    LOG(D) << "Entering MsedSetLSP()";
    vector<uint8_t> table;
    table.push_back(0xa8);
    for (int i = 0; i < 8; i++) {
        table.push_back(OPALUID[table_uid][i]);
    }
    MsedDev *device = new MsedDev(devref);
    if (!(device->isOpal2())) {
        LOG(E) << "Device not Opal2 " << devref;
        delete device;
        return 0xff;
    }
    MsedSession * session = new MsedSession(device);
    // session[0:0]->SMUID.StartSession[HostSessionID:HSN, SPID : LockingSP_UID, Write : TRUE,
    //               HostChallenge = <password>, HostSigningAuthority = Admin1_UID]
    if (session->start(OPAL_UID::OPAL_LOCKINGSP_UID, password, OPAL_UID::OPAL_ADMIN1_UID)) {
        delete session;
        delete device;
        return 0xff;
    }
    if (setTable(session, table, name, value)) {
        LOG(E) << "Unable to update table";
        delete session;
        delete device;
        return 0xff;
    }
    MsedResponse response;
    //getTable(session, table, 1, 10, response);
    LOG(I) << "New value set ";
    // session[TSN:HSN] <- EOS
    delete session;
    delete device;
    LOG(D) << "Exiting MsedSetLSP()";
    return 0;
}


