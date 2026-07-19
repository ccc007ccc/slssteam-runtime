#pragma once

class CProtoBufMsgBase;

namespace ContentHooks
{
	void sendMsg(CProtoBufMsgBase* msg);
	void recvMsg(CProtoBufMsgBase* msg);
}
