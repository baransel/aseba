/*
	Aseba - an event-based framework for distributed robot control
	Copyright (C) 2007--2011:
		Stephane Magnenat <stephane at magnenat dot net>
		(http://stephane.magnenat.net)
		and other contributors, see authors.txt for details
	
	This program is free software: you can redistribute it and/or modify
	it under the terms of the GNU Lesser General Public License as published
	by the Free Software Foundation, version 3 of the License.
	
	This program is distributed in the hope that it will be useful,
	but WITHOUT ANY WARRANTY; without even the implied warranty of
	MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
	GNU Lesser General Public License for more details.
	
	You should have received a copy of the GNU Lesser General Public License
	along with this program. If not, see <http://www.gnu.org/licenses/>.
*/

#include <cstdlib>
#include <cstring>
#include <iostream>
#include <sstream>
#include <set>
#include <valarray>
#include <vector>
#include <iterator>
#include "switch.h"
#include "../common/consts.h"
#include "../common/types.h"
#include "../utils/utils.h"
#include "../msg/msg.h"
#include "../msg/endian.h"

namespace Aseba 
{
	using namespace std;
	using namespace Dashel;
	
	/** \addtogroup switch */
	/*@{*/

	//! Broadcast messages form any data stream to all others data streams including itself.
	Switch::Switch(unsigned port, bool verbose, bool dump, bool forward, bool rawTime) :
		#ifdef DASHEL_VERSION_INT
		Dashel::Hub(verbose || dump),
		#endif // DASHEL_VERSION_INT
		verbose(verbose),
		dump(dump),
		forward(forward),
		rawTime(rawTime)
	{
		ostringstream oss;
		oss << "tcpin:port=" << port;
		connect(oss.str());
	}
	
	void Switch::connectionCreated(Stream *stream)
	{
		if (verbose)
		{
			dumpTime(cout, rawTime);
			cout << "Incoming connection from " << stream->getTargetName() << endl;
		}
	}
	
	void Switch::incomingData(Stream *stream)
	{
		// max packet length is 65533
		// packet source and packet type is not counted in len,
		// thus amount of data to read is of size: 
		// len + 2 (for source id) + 2 (for msg type)
		uint16 netLen;
		
		// read the transfer size
		stream->read(&netLen, 2);
		const uint16 len(swapEndianCopy(netLen));
		
		// read the source id
		uint16 netSourceId;
		stream->read(&netSourceId, 2);
		uint16 sourceId(swapEndianCopy(netSourceId));
		uint16 oldSourceId(sourceId);
		
		// check whether it is remapped
		const IdRemapTable::const_iterator remapIt(idRemapTable.find(stream));
		if (remapIt != idRemapTable.end())
		{
			sourceId = remapIt->second;
			netSourceId = swapEndianCopy(sourceId);
		}
		
		// read the type
		uint16 netType;
		stream->read(&netType, 2);
		const uint16 type(swapEndianCopy(netType));
		
		// allocate the read buffer and do socket read
		std::valarray<uint8> readbuff((uint8)0, len);
		stream->read(&readbuff[0], len);
		
		if (dump)
		{
			dumpTime(cout, rawTime);
			std::cout << "From " << sourceId;
			if (oldSourceId != sourceId)
				std::cout << " (remapped from " << oldSourceId << ")";
			std::cout << ", type " << std::hex << type << ", reading " << std::dec << len << " content bytes on stream " << stream << " : ";
			for(unsigned int i = 0; i < readbuff.size(); i++)
				std::cout << std::hex << (unsigned)readbuff[i] << " ";
			std::cout << std::endl;
		}
		
		// write on all connected streams
		for (StreamsSet::iterator it = dataStreams.begin(); it != dataStreams.end();++it)
		{
			Stream* destStream = *it;
			
			if ((forward) && (destStream == stream))
				continue;
			
			try
			{
				destStream->write(&netLen, 2);
				destStream->write(&netSourceId, 2);
				destStream->write(&netType, 2);
				destStream->write(&readbuff[0], len);
				destStream->flush();
			}
			catch (DashelException e)
			{
				// if this stream has a problem, ignore it for now, and let Hub call connectionClosed later.
				std::cerr << "error while writing" << std::endl;
			}
		}
	}
	
	void Switch::connectionClosed(Stream *stream, bool abnormal)
	{
		if (verbose)
		{
			dumpTime(cout);
			if (abnormal)
				cout << "Abnormal connection closed to " << stream->getTargetName() << " : " << stream->getFailReason() << endl;
			else
				cout << "Normal connection closed to " << stream->getTargetName() << endl;
		}
	}
	
	void Switch::broadcastDummyUserMessage()
	{
		Aseba::UserMessage uMsg;
		uMsg.type = 0;
		//for (int i = 0; i < 80; i++)
		for (StreamsSet::iterator it = dataStreams.begin(); it != dataStreams.end();++it)
		{
			uMsg.serialize(*it);
			(*it)->flush();
		}
	}
	
	void Switch::remapId(Dashel::Stream* stream, const unsigned id)
	{
		idRemapTable[stream] = id;
	}
	
	/*@}*/
};

//! Show usage
void dumpHelp(std::ostream &stream, const char *programName)
{
	stream << "Aseba switch, connects aseba components together, usage:\n";
	stream << programName << " [options] [additional targets]*\n";
	stream << "Options:\n";
	stream << "-v, --verbose   : makes the switch verbose\n";
	stream << "-d, --dump      : makes the switch dump all data\n";
	stream << "-l, --loop      : makes the switch transmit messages back to the send, not only forward them.\n";
	stream << "-p port         : listens to incoming connection on this port\n";
	stream << "--rawtime       : shows time in the form of sec:usec since 1970\n";
	stream << "-h, --help      : shows this help\n";
	stream << "Additional targets are any valid Dashel targets." << std::endl;
}

int main(int argc, char *argv[])
{
	unsigned port = ASEBA_DEFAULT_PORT;
	bool verbose = false;
	bool dump = false;
	bool forward = true;
	bool rawTime = false;
	std::vector<std::string> additionalTargets;
	
	int argCounter = 1;
	
	while (argCounter < argc)
	{
		const char *arg = argv[argCounter];
		
		if ((strcmp(arg, "-v") == 0) || (strcmp(arg, "--verbose") == 0))
		{
			verbose = true;
		}
		else if ((strcmp(arg, "-d") == 0) || (strcmp(arg, "--dump") == 0))
		{
			dump = true;
		}
		else if ((strcmp(arg, "-l") == 0) || (strcmp(arg, "--loop") == 0))
		{
			forward = false;
		}
		else if (strcmp(arg, "-p") == 0)
		{
			if (argCounter + 1 >= argc)
			{
				std::cerr << "port value needed" << std::endl;
				return 1;
			}
			arg = argv[++argCounter];
			port = atoi(arg);
		}
		else if (strcmp(arg, "--rawtime") == 0)
		{
			rawTime = true;
		}
		else if ((strcmp(arg, "-h") == 0) || (strcmp(arg, "--help") == 0))
		{
			dumpHelp(std::cout, argv[0]);
			return 0;
		}
		else
		{
			additionalTargets.push_back(argv[argCounter]);
		}
		argCounter++;
	}
	
	try
	{
		Aseba::Switch aswitch(port, verbose, dump, forward, rawTime);
		for (size_t i = 0; i < additionalTargets.size(); i++)
		{
			const std::string& target(additionalTargets[i]);
			Dashel::Stream* stream = aswitch.connect(target);
			
			// see whether we have to remap the id of this stream
			Dashel::ParameterSet remapIdDecoder;
			remapIdDecoder.add("remap=-1");
			remapIdDecoder.add(target.c_str());
			const int remappedId(remapIdDecoder.get<int>("remap"));
			if (remappedId >= 0)
				aswitch.remapId(stream, unsigned(remappedId));
		}
		/*
		Uncomment this and comment aswitch.run() to flood all pears with dummy user messages
		while (1)
		{
			aswitch.step(10);
			aswitch.broadcastDummyUserMessage();
		}*/
		aswitch.run();
	}
	catch(Dashel::DashelException e)
	{
		std::cerr << e.what() << std::endl;
	}
	
	return 0;
}


