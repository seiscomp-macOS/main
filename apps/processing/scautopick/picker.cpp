/***************************************************************************
 * Copyright (C) GFZ Potsdam                                               *
 * All rights reserved.                                                    *
 *                                                                         *
 * GNU Affero General Public License Usage                                 *
 * This file may be used under the terms of the GNU Affero                 *
 * Public License version 3.0 as published by the Free Software Foundation *
 * and appearing in the file LICENSE included in the packaging of this     *
 * file. Please review the following information to ensure the GNU Affero  *
 * Public License version 3.0 requirements will be met:                    *
 * https://www.gnu.org/licenses/agpl-3.0.html.                             *
 ***************************************************************************/


#define SEISCOMP_COMPONENT Autopick
#include <seiscomp/logging/log.h>

#include <seiscomp/client/inventory.h>

#include <seiscomp/processing/application.h>
#include <seiscomp/processing/response.h>
#include <seiscomp/processing/sensor.h>

#include <seiscomp/io/archive/xmlarchive.h>

#include <seiscomp/math/geo.h>
#include <seiscomp/math/filter.h>

#include <seiscomp/seismology/magnitudes.h>

#include <seiscomp/datamodel/pick.h>
#include <seiscomp/datamodel/amplitude.h>
#include <seiscomp/datamodel/notifier.h>
#include <seiscomp/datamodel/sensorlocation.h>
#include <seiscomp/datamodel/stream.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/datamodel/config_package.h>

#include <seiscomp/utils/misc.h>

#include <iomanip>
#include <functional>

#include "picker.h"
#include "detector.h"


using namespace std;
using namespace Seiscomp::Client;
using namespace Seiscomp::Math;
using namespace Seiscomp::Processing;


#define LOG_PICKS


namespace {


char statusFlag(const Seiscomp::DataModel::Pick *pick) {
	try {
		if ( pick->evaluationMode() == Seiscomp::DataModel::AUTOMATIC ) {
			return 'A';
		}
		else {
			return 'M';
		}
	}
	catch ( ... ) {}
	return 'A';
}


bool contains(const Seiscomp::Core::TimeWindow &tw, const OPT(Seiscomp::Core::Time) &time) {
	if ( !time ) {
		return false;
	}

	if ( tw.startTime().valid() && tw.endTime().valid() ) {
		return tw.contains(*time);
	}

	if ( tw.startTime().valid() ) {
		return *time >= tw.startTime();
	}

	if ( tw.endTime().valid() ) {
		return *time < tw.endTime();
	}

	return true;
}


Seiscomp::DataModel::WaveformStreamID waveformStreamID(const Seiscomp::Record *rec) {
	return Seiscomp::DataModel::WaveformStreamID(
		rec->networkCode(), rec->stationCode(), rec->locationCode(), rec->channelCode(), "");
}


std::string dotted(const Seiscomp::DataModel::WaveformStreamID &wf) {
	return wf.networkCode() + "." + wf.stationCode() + "." + wf.locationCode() + "." + wf.channelCode();
}


/*
ostream& operator<<(ostream& o, const Seiscomp::Core::Time& time) {
	o << time.toString("%Y/%m/%d %H:%M:%S.") << (time.microseconds() / 1000);
	return o;
}

ostream& operator<<(ostream& o, const Seiscomp::DataModel::Pick* pick) {
	o
	 << setiosflags(ios::left)
	 << "BEGIN " << pick->publicID() << endl
	 << "    " << setw(17) << "net" << "= " << pick->waveformID().networkCode() << endl
	 << "    " << setw(17) << "sta" << "= " << pick->waveformID().stationCode() << endl
	 << "    " << setw(17) << "loc" << "= " << pick->waveformID().locationCode() << endl
	 << "    " << setw(17) << "cha" << "= " << pick->waveformID().channelCode() << endl
	 << "    " << setw(17) << "on"  << "= " << pick->time().value() << endl
	 << "    " << setw(17) << "phase" << "= " << pick->phaseHint().code() << endl;
	try {
		o << "    " << setw(17) << "timestamp"  << "= " << pick->creationInfo().creationTime() << endl;
	}
	catch ( Seiscomp::Core::ValueException& ) {}
	o
	 << "END" << endl;

	return o;
}

ostream& operator<<(ostream& o, const Seiscomp::DataModel::Amplitude* amp) {
	o
	 << setiosflags(ios::left)
	 << "BEGIN " << amp->publicID() << endl
	 << "    " << setw(17) << "net" << "= " << amp->waveformID().networkCode() << endl
	 << "    " << setw(17) << "sta" << "= " << amp->waveformID().stationCode() << endl
	 << "    " << setw(17) << "loc" << "= " << amp->waveformID().locationCode() << endl
	 << "    " << setw(17) << "cha" << "= " << amp->waveformID().channelCode() << endl
	 << "    " << setw(17) << "on"  << "= " << (amp->timeWindow().reference() + Seiscomp::Core::TimeSpan(amp->timeWindow().begin())) << endl
	 << "    " << setw(17) << "off"  << "= " << (amp->timeWindow().reference() + Seiscomp::Core::TimeSpan(amp->timeWindow().end())) << endl
	 << "    " << setw(17) << "period"  << "= ";

	try {
		o << amp->period().value() << endl;
	}
	catch ( ... ) {
		o << "N.D." << endl;
	}

	o << "    " << setw(17) << "ampl_" + amp->type() << "= " << amp->amplitude().value() << endl;
	if ( !amp->pickID().empty() )
		o << "    " << setw(17) << "pick" << "= " << amp->pickID() << endl;
	try {
		o << "    " << setw(17) << "timestamp"  << "= " << amp->creationInfo().creationTime() << endl;
	}
	catch ( Seiscomp::Core::ValueException& ) {}
	o << "END" << endl;

	return o;
}
*/

}


namespace Seiscomp {
namespace Applications {
namespace Picker {
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
App::App(int argc, char **argv) : Processing::Application(argc, argv) {
	setLoadInventoryEnabled(true);
	setLoadConfigModuleEnabled(true);

	setPrimaryMessagingGroup("PICK");
	addMessagingSubscription("CONFIG");
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
App::~App() {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::createCommandLineDescription() {
	StreamApplication::createCommandLineDescription();
	commandline().addOption("Database", "db-disable", "Do not use the database at all.");

	commandline().addGroup("Mode");
	commandline().addOption("Mode", "offline", "Do not connect to a messaging server.");
	commandline().addOption("Mode", "amplitudes", "Enable/disable computation of amplitudes.",
	                        &_config.calculateAmplitudes);
	commandline().addOption("Mode", "test", "Do not send any object.");
	commandline().addOption("Mode", "playback",
	                        "Use playback mode that does not set a request time window and works best with files.");
	commandline().addOption("Mode", "ep",
	                        "Same as offline but outputs all result as an event "
	                        "parameters XML file. Consider '--playback' or "
	                        "configure accordingly for processing data from the past.");
	commandline().addOption("Mode", "dump-config", "Dump the configuration and exit.");
	commandline().addOption("Mode", "dump-records",
	                        "Dump records to ASCII when in offline mode.");

	commandline().addGroup("Settings");
	commandline().addOption("Settings", "filter", "The filter used for picking.",
	                        &_config.defaultFilter, false);
	commandline().addOption("Settings", "time-correction", "The time correction in seconds for a pick.",
	                        &_config.defaultTimeCorrection);
	commandline().addOption("Settings", "buffer-size", "The waveform ringbuffer size in seconds.",
	                        &_config.ringBufferSize);
	commandline().addOption("Settings", "before", "The timespan in seconds before now to start picking.",
	                        &_config.leadTime);
	commandline().addOption("Settings", "init-time",
	                        "The initialization (inactive) time after the first record arrived per trace.",
	                        &_config.initTime);

	commandline().addOption("Settings", "trigger-on", "The trigger-on threshold.",
	                        &_config.defaultTriggerOnThreshold);
	commandline().addOption("Settings", "trigger-off", "The trigger-off threshold.",
	                        &_config.defaultTriggerOffThreshold);
	commandline().addOption("Settings", "trigger-dead-time",
	                        "The dead-time after a pick has been detected.",
	                        &_config.triggerDeadTime);
	commandline().addOption("Settings", "ampl-max-time-window",
	                        "The timewindow length after pick to calculate 'max' amplitude",
	                        &_config.amplitudeMaxTimeWindow);
	commandline().addOption("Settings", "min-ampl-offset",
	                        "The amplitude offset for amplitude dependend dead time calculation.",
	                        &_config.amplitudeMinOffset);
	commandline().addOption("Settings", "gap-tolerance",
	                        "The maximum gap length to tolerate (reset otherwise).",
	                        &_config.maxGapLength);
	commandline().addOption("Settings", "gap-interpolation",
	                        "Enables/disables the linear interpolation of gaps",
	                        &_config.interpolateGaps);
	commandline().addOption("Settings", "any-stream",
	                        "Use all/configured received Z streams for picking.",
	                        &_config.useAllStreams);
	commandline().addOption("Settings", "send-detections",
	                        "If a picker is configured send detections as well.");
	commandline().addOption("Settings", "extra-comments",
	                        "Add extra comments to picks.\nSupported: SNR.");

	commandline().addGroup("Output");
	commandline().addOption("Output", "formatted,f",
	                        "Use formatted XML output. Otherwise XML is unformatted.");
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::validateParameters() {
	if ( !StreamApplication::validateParameters() ) {
		return false;
	}

	_config.init(commandline());
	setMessagingEnabled(!_config.offline);
	bool disableDB = commandline().hasOption("db-disable") ||
	                 (!isInventoryDatabaseEnabled() && !isConfigDatabaseEnabled());

	setDatabaseEnabled(!disableDB, true);

	if ( _config.ringBufferSize < 0 ) {
		cerr << "The buffer-size must not be negative" << endl;
		return false;
	}

	if ( _config.initTime < 0 ) {
		cerr << "The init-time must not be negative" << endl;
		return false;
	}

	if ( _config.triggerDeadTime < 0 ) {
		cerr << "The trigger dead-time must not be negative" << endl;
		return false;
	}

	if ( _config.amplitudeMaxTimeWindow < 0 ) {
		cerr << "The amplitude-max-time-window must not be negative" << endl;
		return false;
	}

	if ( _config.amplitudeMinOffset < 0 ) {
		cerr << "The min-amplitude-offset must not be negative" << endl;
		return false;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::initConfiguration() {
	if ( !Processing::Application::initConfiguration() )
		return false;

	_config.init(this);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::init() {
	if ( !StreamApplication::init() ) return false;

	_logPicks = addOutputObjectLog("pick", primaryMessagingGroup());
	_logAmps = addOutputObjectLog("amplitude", _config.amplitudeGroup);

	if ( !_config.pickerType.empty() ) {
		// Check availability of configured picker type
		PickerPtr proc = PickerFactory::Create(_config.pickerType.c_str());
		if ( !proc ) {
			SEISCOMP_ERROR("Picker '%s' is not available, abort application", _config.pickerType.c_str());
			return false;
		}
	}

	_stationConfig.setDefault(
		StreamConfig(_config.defaultTriggerOnThreshold,
		             _config.defaultTriggerOffThreshold,
		             _config.defaultTimeCorrection,
		             _config.defaultFilter)
	);

	streamBuffer().setTimeSpan(_config.ringBufferSize);

	if ( commandline().hasOption("playback") )
		_config.playback = true;

	// Only set start time if playback option is not set. Without a time window
	// set, a file source will forward all records to the application otherwise
	// they are filtered according to the time window.
	if ( _config.playback) {
		SEISCOMP_DEBUG("Starting in playback mode");
	}
	else {
		recordStream()->setStartTime(Core::Time::UTC() - Core::TimeSpan(_config.leadTime));
	}

	if ( configModule() != NULL ) {
		_config.useAllStreams = false;
		//cerr << "Reading configured streams:" << endl;

		_stationConfig.read(&configuration(), configModule(), name());
	}

	// Components to acquire
	bool acquireComps[3];
	acquireComps[0] = true;
	acquireComps[1] = false;
	acquireComps[2] = false;

	if ( !_config.pickerType.empty() ) {
		PickerPtr proc = PickerFactory::Create(_config.pickerType.c_str());
		if ( proc == NULL ) {
			SEISCOMP_ERROR("Unknown picker: %s", _config.pickerType.c_str());
			return false;
		}

		if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
		     proc->usedComponent() == WaveformProcessor::Any ||
		     proc->usedComponent() == WaveformProcessor::FirstHorizontal ) {
			acquireComps[1] = true;
		}

		if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
		     proc->usedComponent() == WaveformProcessor::Any ||
		     proc->usedComponent() == WaveformProcessor::SecondHorizontal ) {
			acquireComps[2] = true;
		}
	}

	if ( !_config.secondaryPickerType.empty() ) {
		SecondaryPickerPtr proc = SecondaryPickerFactory::Create(_config.secondaryPickerType.c_str());
		if ( proc == NULL ) {
			SEISCOMP_ERROR("Unknown secondary picker: %s", _config.secondaryPickerType.c_str());
			return false;
		}

		if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
		     proc->usedComponent() == WaveformProcessor::Any ||
		     proc->usedComponent() == WaveformProcessor::FirstHorizontal ) {
			acquireComps[1] = true;
		}

		if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
		     proc->usedComponent() == WaveformProcessor::Any ||
		     proc->usedComponent() == WaveformProcessor::SecondHorizontal ) {
			acquireComps[2] = true;
		}
	}

	if ( !_config.featureExtractionType.empty() ) {
		FXPtr proc = FXFactory::Create(_config.featureExtractionType.c_str());
		if ( proc == NULL ) {
			SEISCOMP_ERROR("Unknown fx: %s", _config.featureExtractionType.c_str());
			return false;
		}

		if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
		     proc->usedComponent() == WaveformProcessor::Any ||
		     proc->usedComponent() == WaveformProcessor::FirstHorizontal ) {
			acquireComps[1] = true;
		}

		if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
		     proc->usedComponent() == WaveformProcessor::Any ||
		     proc->usedComponent() == WaveformProcessor::SecondHorizontal ) {
			acquireComps[2] = true;
		}
	}

	std::string logAmplTypes;
	for ( StringSet::iterator it = _config.amplitudeList.begin();
	      it != _config.amplitudeList.end(); ) {
		AmplitudeProcessorPtr proc = AmplitudeProcessorFactory::Create(it->c_str());
		logAmplTypes += " * ";
		if ( !proc ) {
			logAmplTypes += *it;
			logAmplTypes += ": Disabled (unknown type)";
			_config.amplitudeList.erase(it++);
		}
		else {
			logAmplTypes += *it;
			logAmplTypes += ": OK";
			if ( _config.amplitudeUpdateList.find(*it) != _config.amplitudeUpdateList.end() )
				logAmplTypes += " (updates enabled)";
			++it;

			if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
			     proc->usedComponent() == WaveformProcessor::Any ||
			     proc->usedComponent() == WaveformProcessor::FirstHorizontal ) {
				acquireComps[1] = true;
			}

			if ( proc->usedComponent() == WaveformProcessor::Horizontal ||
			     proc->usedComponent() == WaveformProcessor::Any ||
			     proc->usedComponent() == WaveformProcessor::SecondHorizontal ) {
				acquireComps[2] = true;
			}
		}

		logAmplTypes += '\n';
	}

	Core::Time now = Core::Time::UTC();
	DataModel::Inventory *inv = Client::Inventory::Instance()->inventory();

	StringSet subscribeStreams;

	for ( StationConfig::const_iterator it = _stationConfig.begin();
	      it != _stationConfig.end(); ++it ) {
		// Ignore wildcards
		if ( it->first.first == "*" ) continue;
		if ( it->first.second == "*" ) continue;

		// Ignore undefined channels
		if ( it->second.channel.empty() ) continue;

		// Ignore disabled channels
		if ( !it->second.enabled ) {
			SEISCOMP_INFO("Detector on station %s.%s disabled by config",
			              it->first.first, it->first.second);
			continue;
		}

		DataModel::SensorLocation *loc =
			Client::Inventory::Instance()->getSensorLocation(it->first.first, it->first.second, it->second.locationCode, now);

		string channel = it->second.channel;
		char compCode = 'Z';
		bool isFixedChannel = channel.size() > 2;

		DataModel::ThreeComponents chan;

		if ( loc )
			DataModel::getThreeComponents(chan, loc, it->second.channel.substr(0,2).c_str(), now);

		DataModel::Stream *compZ = chan.comps[DataModel::ThreeComponents::Vertical];
		DataModel::Stream *compN = chan.comps[DataModel::ThreeComponents::FirstHorizontal];
		DataModel::Stream *compE = chan.comps[DataModel::ThreeComponents::SecondHorizontal];

		if ( !isFixedChannel ) {
			if ( compZ )
				channel = compZ->code();
			else
				channel += compCode;
		}

		std::string streamID = it->first.first + "." + it->first.second + "." + it->second.locationCode + "." + channel;

		SEISCOMP_INFO("Adding detection channel %s", streamID.c_str());

		_streamIDs.insert(streamID);
		subscribeStreams.insert(streamID);

		recordStream()->addStream(it->first.first, it->first.second, it->second.locationCode, channel);

		if ( compZ && acquireComps[0] ) {
			streamID = it->first.first + "." + it->first.second + "." + it->second.locationCode + "." + compZ->code();
			if ( subscribeStreams.find(streamID) == subscribeStreams.end() ) {
				SEISCOMP_DEBUG("Adding data channel %s", streamID.c_str());
				recordStream()->addStream(it->first.first, it->first.second, it->second.locationCode, compZ->code());
				subscribeStreams.insert(streamID);
			}
		}

		if ( compN && acquireComps[1] ) {
			streamID = it->first.first + "." + it->first.second + "." + it->second.locationCode + "." + compN->code();
			if ( subscribeStreams.find(streamID) == subscribeStreams.end() ) {
				SEISCOMP_DEBUG("Adding data channel %s", streamID.c_str());
				recordStream()->addStream(it->first.first, it->first.second, it->second.locationCode, compN->code());
				subscribeStreams.insert(streamID);
			}
		}

		if ( compE && acquireComps[2] ) {
			streamID = it->first.first + "." + it->first.second + "." + it->second.locationCode + "." + compE->code();
			if ( subscribeStreams.find(streamID) == subscribeStreams.end() ) {
				SEISCOMP_DEBUG("Adding data channel %s", streamID.c_str());
				recordStream()->addStream(it->first.first, it->first.second, it->second.locationCode, compE->code());
				subscribeStreams.insert(streamID);
			}
		}

		if ( _config.playback && inv ) {
			// Figure out all historic epochs for locations and channels and
			// subscribe to all channels covering all epochs. This is only
			// required in playback mode if historic data is fed into the picker
			// and where the current time check does not apply.

			for ( size_t n = 0; n < inv->networkCount(); ++n ) {
				DataModel::Network *net = inv->network(n);
				if ( net->code() != it->first.first ) continue;

				for ( size_t s = 0; s < net->stationCount(); ++s ) {
					DataModel::Station *sta = net->station(s);
					if ( sta->code() != it->first.second ) continue;

					for ( size_t l = 0; l < sta->sensorLocationCount(); ++l ) {
						DataModel::SensorLocation *loc = sta->sensorLocation(l);
						if ( loc->code() != it->second.locationCode ) continue;

						for ( size_t c = 0; c < loc->streamCount(); ++c ) {
							DataModel::Stream *cha = loc->stream(c);

							if ( it->second.channel.compare(0, 2, cha->code(), 0, 2) == 0 ) {
								streamID = it->first.first + "." + it->first.second + "." + it->second.locationCode + "." + cha->code();
								if ( subscribeStreams.find(streamID) == subscribeStreams.end() ) {
									SEISCOMP_DEBUG("Adding data channel %s", streamID.c_str());
									recordStream()->addStream(it->first.first, it->first.second, it->second.locationCode, cha->code());
									subscribeStreams.insert(streamID);
								}
							}
						}
					}
				}
			}
		}
	}


	if ( _streamIDs.empty() ) {
		if ( _config.useAllStreams )
			SEISCOMP_INFO("No stations added (empty module configuration?)");
		else {
			SEISCOMP_ERROR("No stations added (empty module configuration?) and thus nothing to do");
			return false;
		}
	}
	else
		SEISCOMP_INFO("%d channels added", (int)_streamIDs.size());

	SEISCOMP_INFO("\nAmplitudes to calculate:\n%s", logAmplTypes.c_str());

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::printUsage() const {
	cout << "Usage:"  << endl << "  " << name() << " [options]" << endl << endl
	     << "Detect P and S phases creating phase picks and amplitudes" << endl;

	Seiscomp::Client::Application::printUsage();

	cout << "Examples:" << endl;
	cout << "Real-time processing with informative debug output" << endl
	     << "  " << name() << " --debug" << endl << endl;
	cout << "Non-real-time playback of miniSEED data in a file. Picks and "
	        "amplitudes are printed to stdout as SCML" << endl
	     << "  " << name() << " -d localhost --playback --ep -I data.mseed" << endl << endl;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::run() {
	if ( commandline().hasOption("dump-config") ) {
		_config.dump();
		printf("\n");
		_stationConfig.dump();
		return true;
	}

	if ( commandline().hasOption("ep") ) {
		_ep = new DataModel::EventParameters;
	}

	_formatted = commandline().hasOption("formatted");

	return Processing::Application::run();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::done() {
	if ( _ep ) {
		IO::XMLArchive ar;
		ar.create("-");
		ar.setFormattedOutput(_formatted);
		ar << _ep;
		ar.close();
		cerr << "Found "<< _ep->pickCount() << " picks and "
		     << _ep->amplitudeCount() << " amplitudes" << endl;
		_ep = NULL;
	}

	Processing::Application::done();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::addObject(const string& parentID, DataModel::Object* o) {
	Processing::Application::addObject(parentID, o);

	// TODO: handle QC objects
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::removeObject(const string& parentID, DataModel::Object* o) {
	Processing::Application::removeObject(parentID, o);

	// TODO: handle QC objects
	//       call enableStation or enableStream to enable/disable a
	//       certain station/stream
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::updateObject(const string& parentID, DataModel::Object* o) {
	Processing::Application::updateObject(parentID, o);

	// TODO: handle QC objects
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::initComponent(Processing::WaveformProcessor *proc,
                        Processing::WaveformProcessor::Component comp,
                        const Core::Time &time,
                        const std::string &streamID,
                        const DataModel::WaveformStreamID &waveformID,
                        bool metaDataRequired) {
	auto it = _streams.find(streamID);
	if ( it != _streams.end() && contains(it->second->epoch, time) ) {
		proc->streamConfig(comp) = *it->second;
		if ( proc->streamConfig(comp).gain == 0.0 && metaDataRequired ) {
			SEISCOMP_ERROR("No gain for stream %s", streamID.c_str());
			return false;
		}
	}
	else {
		// Load sensor, responses usw.
		Processing::StreamPtr stream = new Processing::Stream;
		_streams[streamID] = stream;

		stream->init(waveformID.networkCode(), waveformID.stationCode(), waveformID.locationCode(), waveformID.channelCode(), time);
		if ( (stream->gain == 0.0) && metaDataRequired ) {
			SEISCOMP_ERROR("No gain for stream %s", streamID.c_str());
			return false;
		}

		proc->streamConfig(comp) = *stream;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::initProcessor(Processing::WaveformProcessor *proc,
                        Processing::WaveformProcessor::StreamComponent comp,
                        const Core::Time &time,
                        const std::string &streamID,
                        const DataModel::WaveformStreamID &waveformID,
                        bool metaDataRequired) {
	const std::string dottedWaveformID = dotted(waveformID);
	switch ( comp ) {
		case Processing::WaveformProcessor::Vertical:
			if ( !initComponent(proc,
			                    Processing::WaveformProcessor::VerticalComponent,
			                    time, streamID, waveformID, metaDataRequired) ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": failed to setup vertical component");
				return false;
			}
			break;

		case Processing::WaveformProcessor::FirstHorizontal:
			if ( !initComponent(proc,
			                    Processing::WaveformProcessor::FirstHorizontalComponent,
			                    time, streamID, waveformID, metaDataRequired) ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": failed to setup first horizontal component");
				return false;
			}
			break;

		case Processing::WaveformProcessor::SecondHorizontal:
			if ( !initComponent(proc, Processing::WaveformProcessor::SecondHorizontalComponent,
			                    time, streamID, waveformID, metaDataRequired) ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": failed to setup second horizontal component");
				return false;
			}
			break;

		case Processing::WaveformProcessor::Horizontal:
		{
			// Find the two horizontal components of given location code
			DataModel::ThreeComponents chans;
			DataModel::WaveformStreamID waveformID1, waveformID2;
			string streamID1, streamID2;

			DataModel::SensorLocation *loc =
				Client::Inventory::Instance()->getSensorLocation(
					waveformID.networkCode(), waveformID.stationCode(), waveformID.locationCode(), time);

			if ( loc == NULL ) {
				SEISCOMP_ERROR("%s.%s: location code '%s' not found",
				               waveformID.networkCode().c_str(), waveformID.stationCode().c_str(),
				               waveformID.locationCode().c_str());
				return false;
			}

			// Extract the first two characters of the channel code
			string c2 = waveformID.channelCode().substr(0, 2);

			DataModel::getThreeComponents(chans, loc, c2.c_str(), time);
			if ( chans.comps[DataModel::ThreeComponents::FirstHorizontal] != NULL ) {
				string channelCode1 = chans.comps[DataModel::ThreeComponents::FirstHorizontal]->code();
				waveformID1 = waveformID;
				waveformID1.setChannelCode(channelCode1);
				streamID1 = dotted(waveformID1);
			}
			else {
				SEISCOMP_ERROR_S(dottedWaveformID + ": 1st horizontal component not available");
				return false;
			}

			if ( chans.comps[DataModel::ThreeComponents::SecondHorizontal] != NULL ) {
				string channelCode2 = chans.comps[DataModel::ThreeComponents::SecondHorizontal]->code();
				waveformID2 = waveformID;
				waveformID2.setChannelCode(channelCode2);
				streamID2 = dotted(waveformID2);
			}
			else {
				SEISCOMP_ERROR_S(dottedWaveformID + ": 2nd horizontal component not available");
				return false;
			}

			if ( !initComponent(proc, Processing::WaveformProcessor::FirstHorizontalComponent,
			                    time, streamID1, waveformID1, metaDataRequired) ||
			     !initComponent(proc, Processing::WaveformProcessor::SecondHorizontalComponent,
			                    time, streamID2, waveformID2, metaDataRequired) ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": failed to setup horizontal components");
				return false;
			}
			break;
		}

		case Processing::WaveformProcessor::Any:
		{
			// Find the all three components of given location code
			DataModel::ThreeComponents chans;
			DataModel::WaveformStreamID waveformID0, waveformID1, waveformID2;
			string streamID0, streamID1, streamID2;

			DataModel::SensorLocation *loc =
				Client::Inventory::Instance()->getSensorLocation(
					waveformID.networkCode(), waveformID.stationCode(), waveformID.locationCode(), time
				);

			if ( loc == NULL ) {
				SEISCOMP_ERROR("%s.%s: location code '%s' not found",
				               waveformID.networkCode().c_str(), waveformID.stationCode().c_str(),
				               waveformID.locationCode().c_str());
				return false;
			}

			// Extract the first two characters of the channel code
			string c2 = waveformID.channelCode().substr(0, 2);
			DataModel::getThreeComponents(chans, loc, c2.c_str(), time);
			if ( chans.comps[DataModel::ThreeComponents::Vertical] != NULL ) {
				string channelCode0 = chans.comps[DataModel::ThreeComponents::Vertical]->code();
				waveformID0 = waveformID;
				waveformID0.setChannelCode(channelCode0);
				streamID0 = dotted(waveformID0);
			}
			else if ( metaDataRequired ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": vertical component not available");
				return false;
			}

			if ( chans.comps[DataModel::ThreeComponents::FirstHorizontal] != NULL ) {
				string channelCode1 = chans.comps[DataModel::ThreeComponents::FirstHorizontal]->code();
				waveformID1 = waveformID;
				waveformID1.setChannelCode(channelCode1);
				streamID1 = dotted(waveformID1);
			}
			else if ( metaDataRequired ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": 1st horizontal component not available");
				return false;
			}

			if ( chans.comps[DataModel::ThreeComponents::SecondHorizontal] != NULL ) {
				string channelCode2 = chans.comps[DataModel::ThreeComponents::SecondHorizontal]->code();
				waveformID2 = waveformID;
				waveformID2.setChannelCode(channelCode2);
				streamID2 = dotted(waveformID2);
			}
			else if ( metaDataRequired ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": 2nd horizontal component not available");
				return false;
			}

			if ( ! initComponent(proc, Processing::WaveformProcessor::VerticalComponent,
			                     time, streamID0, waveformID0, metaDataRequired) ||
			     ! initComponent(proc, Processing::WaveformProcessor::FirstHorizontalComponent,
			                     time, streamID1, waveformID1, metaDataRequired) ||
			     ! initComponent(proc, Processing::WaveformProcessor::SecondHorizontalComponent,
			                     time, streamID2, waveformID2, metaDataRequired) ) {
				SEISCOMP_ERROR_S(dottedWaveformID + ": failed to setup components");
				return false;
			}
			break;
		}

		default:
			break;
	}

	const StreamConfig *sc = _stationConfig.get(&configuration(), configModuleName(),
	                                            waveformID.networkCode(), waveformID.stationCode());
	return proc->setup(Settings(configModuleName(), waveformID.networkCode(), waveformID.stationCode(),
	                            waveformID.locationCode(), waveformID.channelCode(), &configuration(),
	                            sc?sc->parameters.get():NULL));
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::initDetector(const string &streamID,
                       const DataModel::WaveformStreamID &waveformID,
                       const Record *rec) {
	double trigOn = _config.defaultTriggerOnThreshold;
	double trigOff = _config.defaultTriggerOffThreshold;
	double tcorr = _config.defaultTimeCorrection;
	string filter = _config.defaultFilter;
	bool sensitivityCorrection = false;

	const StreamConfig *sc = _stationConfig.get(&configuration(), configModuleName(),
	                                            waveformID.networkCode(), waveformID.stationCode());
	if ( sc != NULL ) {
		if ( !sc->enabled ) {
			SEISCOMP_INFO("Detector on station %s.%s disabled by config",
			              waveformID.networkCode().c_str(), waveformID.stationCode().c_str());
			return true;
		}

		if ( sc->triggerOn ) trigOn = *sc->triggerOn;
		if ( sc->triggerOff ) trigOff = *sc->triggerOff;
		if ( !sc->filter.empty() ) filter = sc->filter;
		if ( sc->timeCorrection ) tcorr = *sc->timeCorrection;
		sensitivityCorrection = sc->sensitivityCorrection;
	}

	DetectorPtr detector = new Detector(trigOn, trigOff, _config.initTime);

	Processing::WaveformProcessor::Filter *detecFilter;
	string filterError;
	detecFilter = Processing::WaveformProcessor::Filter::Create(filter, &filterError);
	if ( !detecFilter ) {
		SEISCOMP_WARNING("%s: compiling filter failed: %s: %s", streamID.c_str(),
		                 filter.c_str(), filterError.c_str());
		return false;
	}

	detector->setDeadTime(_config.triggerDeadTime);
	detector->setAmplitudeTimeWindow(_config.amplitudeMaxTimeWindow);
	//picker->setAmplitudeTimeWindow(0.0);
	detector->setMinAmplitudeOffset(_config.amplitudeMinOffset);
	detector->setDurations(_config.minDuration, _config.maxDuration);
	detector->setFilter(detecFilter);
	detector->setOffset(tcorr);
	detector->setGapTolerance(_config.maxGapLength);
	detector->setGapInterpolationEnabled(_config.interpolateGaps);
	detector->setSensitivityCorrection(sensitivityCorrection);
	detector->setPublishFunction(bind(
		&App::emitDetection, this,
		placeholders::_1,
		placeholders::_2,
		placeholders::_3
	));

	if ( _config.calculateAmplitudes )
		detector->setAmplitudePublishFunction(bind(
			&App::emitAmplitude, this,
			placeholders::_1, placeholders::_2
		));

	if ( !initProcessor(detector.get(), detector->usedComponent(),
	                    rec->startTime(), streamID, waveformID, sensitivityCorrection) )
		return false;

	SEISCOMP_DEBUG("%s: created detector with filter %s",
	               streamID.c_str(), filter.c_str());

	addProcessor(waveformID, detector.get());
	detector->feed(rec);

//	SEISCOMP_DEBUG("Number of processors: %lu", (unsigned long)processorCount());

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::handleNewStream(const Record *rec) {
	if ( _config.useAllStreams || _streamIDs.find(rec->streamID()) != _streamIDs.end() ) {
		if ( !initDetector(rec->streamID(), waveformStreamID(rec), rec) ) {
			SEISCOMP_ERROR("%s: initialization failed: abort operation",
			               rec->streamID().c_str());
			exit(1);
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
template <typename T>
void App::pushProcessor(const std::string &networkCode,
                        const std::string &stationCode,
                        const std::string &locationCode,
                        T *proc) {
	switch ( proc->usedComponent() ) {
		case Processing::WaveformProcessor::Vertical:
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::VerticalComponent).code(),
			             proc);
			break;
		case Processing::WaveformProcessor::FirstHorizontal:
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::FirstHorizontalComponent).code(),
			             proc);
			break;
		case Processing::WaveformProcessor::SecondHorizontal:
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::SecondHorizontalComponent).code(),
			             proc);
			break;
		case Processing::WaveformProcessor::Horizontal:
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::FirstHorizontalComponent).code(),
			             proc);
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::SecondHorizontalComponent).code(),
			             proc);
			break;
		case Processing::WaveformProcessor::Any:
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::VerticalComponent).code(),
			             proc);
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::FirstHorizontalComponent).code(),
			             proc);
			addProcessor(networkCode,
			             stationCode,
			             locationCode,
			             proc->streamConfig(Processing::WaveformProcessor::SecondHorizontalComponent).code(),
			             proc);
			break;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::processorFinished(const Record *rec, WaveformProcessor *wp) {
	std::stringstream ss;

	if ( wp->status() == Processing::WaveformProcessor::LowSNR )
		ss << "SNR " << wp->statusValue() << " too low";
	else if ( wp->status() > Processing::WaveformProcessor::Terminated )
		ss << "ERROR (" << wp->status().toString() << "," << wp->statusValue() << ")";
	else
		ss << "OK";

	SEISCOMP_DEBUG("%s:%s: %s", rec != NULL?rec->streamID().c_str():"-",
	                           wp->className(),
	                           ss.str().c_str());

	// If its a secondary processor remove it from the tracked item list
	ProcReverseMap::iterator pit = _procLookup.find(wp);
	if ( pit == _procLookup.end() ) return;

	ProcMap::iterator mit = _runningStreamProcs.find(pit->second);

	_procLookup.erase(pit);

	if ( mit == _runningStreamProcs.end() ) return;

	ProcList &list = mit->second;
	for ( ProcList::iterator it = list.begin(); it != list.end(); ) {
		if ( it->proc == wp ) {
			SEISCOMP_DEBUG("Removed finished processor from stream procs");
			it = list.erase(it);
		}
		else
			++it;
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool App::addFeatureExtractor(Seiscomp::DataModel::Pick *pick,
                              DataModel::Amplitude *amp,
                              const Record *rec, bool isPrimary) {
	// Add secondary picker
	FXPtr proc = FXFactory::Create(_config.featureExtractionType.c_str());
	if ( !proc ) {
		SEISCOMP_WARNING("Could not create fx: %s", _config.featureExtractionType.c_str());
		this->exit(1);
		return false;
	}

	proc->setTrigger(pick->time().value());
	proc->setEnvironment(
		Client::Inventory::Instance()->getSensorLocation(
			pick->waveformID().networkCode(),
			pick->waveformID().stationCode(),
			pick->waveformID().locationCode(),
			pick->time().value()
		),
		pick
	);
	proc->setPublishFunction(std::bind(
		&App::emitFXPick, this,
		Seiscomp::DataModel::PickPtr(pick),
		Seiscomp::DataModel::AmplitudePtr(amp),
		isPrimary,
		placeholders::_1, placeholders::_2
	));

	const DataModel::WaveformStreamID waveformID(waveformStreamID(rec));
	const std::string &n = rec->networkCode();
	const std::string &s = rec->stationCode();
	const std::string &l = rec->locationCode();
	std::string c = rec->channelCode();

	if ( !initProcessor(proc.get(), proc->usedComponent(), pick->time().value(), rec->streamID(), waveformID, true) )
		return false;

	SEISCOMP_DEBUG("%s: created fx %s (rec ref: %d)",
	               rec->streamID().c_str(), _config.featureExtractionType.c_str(),
	               rec->referenceCount());

	pushProcessor(n, s, l, proc.get());

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::addSecondaryPicker(const Core::Time &onset, const Record *rec, const std::string &pickID) {
	// Add secondary picker
	SecondaryPickerPtr proc = SecondaryPickerFactory::Create(_config.secondaryPickerType.c_str());
	if ( proc == NULL ) {
		SEISCOMP_WARNING("Could not create secondary picker: %s", _config.secondaryPickerType.c_str());
		this->exit(1);
		return;
	}

	SecondaryPicker::Trigger trigger;
	trigger.onset = onset;
	proc->setTrigger(trigger);
	proc->setPublishFunction(bind(&App::emitSPick, this, placeholders::_1, placeholders::_2));
	proc->setReferencingPickID(pickID);

	const DataModel::WaveformStreamID waveformID(waveformStreamID(rec));
	const std::string &n = rec->networkCode();
	const std::string &s = rec->stationCode();
	const std::string &l = rec->locationCode();
	std::string c = rec->channelCode();

	if ( !initProcessor(proc.get(), proc->usedComponent(), onset, rec->streamID(), waveformID, true) )
		return;

	SEISCOMP_DEBUG("%s: created secondary picker %s (rec ref: %d)",
	               rec->streamID().c_str(), _config.secondaryPickerType.c_str(),
	               rec->referenceCount());

	pushProcessor(n, s, l, proc.get());

	ProcList &list = _runningStreamProcs[rec->streamID()];
	if ( _config.killPendingSecondaryProcessors ) {
		SEISCOMP_DEBUG("check for expired procs (got %d in list)", (int)list.size());

		// Check for secondary procs that are still running but where the
		// end time is before onset and remove them
		// ...
		for ( ProcList::iterator it = list.begin(); it != list.end(); ) {
			if ( it->dataEndTime <= onset ) {
				SEISCOMP_DEBUG("Remove expired proc 0x%lx", (long int)it->proc);
				if ( /*it->proc != NULL*/true ) {
					SEISCOMP_INFO("Remove expired running processor %s on %s",
					              it->proc->className(), rec->streamID().c_str());

					if ( it->proc->status() == Processing::WaveformProcessor::LowSNR )
						SEISCOMP_DEBUG("  -> status: SNR(%f) too low", it->proc->statusValue());
					else if ( it->proc->status() > Processing::WaveformProcessor::Terminated )
						SEISCOMP_DEBUG("  -> status: ERROR (%s, %f)",
						               it->proc->status().toString(), it->proc->statusValue());
					else
						SEISCOMP_DEBUG("  -> status: OK");

					// Remove processor from application
					removeProcessor(it->proc);

					// Remove its reverse lookup
					ProcReverseMap::iterator pit = _procLookup.find(it->proc);
					if ( pit != _procLookup.end() ) _procLookup.erase(pit);
				}

				// Remove it from the run list
				it = list.erase(it);
			}
			else
				++it;
		}
	}

	// addProcessor can feed the requested time window with cached records
	// so the proc might be finished already. This needs a test otherwise
	// the registered pointer is invalid later when checking for expired
	// procs.
	if ( !proc->isFinished() ) {
		// Register the secondary procs running on the verticals
		list.push_back(ProcEntry(proc->safetyTimeWindow().endTime(), proc.get()));
		_procLookup[proc.get()] = rec->streamID();
		SEISCOMP_DEBUG("%s: registered proc 0x%lx",
		               rec->streamID().data(), (long int)proc.get());
	}
	else
		SEISCOMP_DEBUG("%s: proc finished already", rec->streamID().data());

//	SEISCOMP_DEBUG("Number of processors: %lu", (unsigned long)processorCount());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::addAmplitudeProcessor(AmplitudeProcessorPtr proc,
                                const Record *rec,
                                const Seiscomp::DataModel::Pick *pick) {
	proc->setPublishFunction(bind(&App::emitAmplitude, this, placeholders::_1, placeholders::_2));
	proc->setReferencingPickID(pick->publicID());

	const DataModel::WaveformStreamID waveformID(waveformStreamID(rec));
	const std::string &n = rec->networkCode();
	const std::string &s = rec->stationCode();
	const std::string &l = rec->locationCode();
	std::string c = rec->channelCode();

	if ( !initProcessor(proc.get(), proc->usedComponent(), proc->trigger(), rec->streamID(), waveformID, true) )
		return;

	proc->setEnvironment(
		nullptr, // No hypocenter information
		Client::Inventory::Instance()->getSensorLocation(
			n, s, l, proc->trigger()
		),
		pick
	);

	if ( proc->isFinished() ) {
		// If the processor has finished already e.g. due to missing
		// hypocenter information, do not add it and return.
		processorFinished(rec, proc.get());
		return;
	}

	if ( _config.amplitudeUpdateList.find(proc->type()) != _config.amplitudeUpdateList.end() )
		proc->setUpdateEnabled(true);
	else
		proc->setUpdateEnabled(false);

	pushProcessor(n, s, l, proc.get());

//	SEISCOMP_DEBUG("Number of processors: %lu", (unsigned long)processorCount());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::emitTrigger(const Processing::Detector *pickProc,
                      const Record *rec, const Core::Time &time) {
	PickerPtr proc = PickerFactory::Create(_config.pickerType.c_str());
	if ( !proc ) {
		SEISCOMP_ERROR("Unable to create '%s' picker, no picking possible", _config.pickerType.c_str());
		return;
	}

	proc->setTrigger(time);
	proc->setPublishFunction(bind(&App::emitPPick, this, placeholders::_1, placeholders::_2, static_cast<const Detector*>(pickProc)->duration()));

	const DataModel::WaveformStreamID waveformID(waveformStreamID(rec));

	if ( !initProcessor(proc.get(), proc->usedComponent(), time, rec->streamID(), waveformID, false) )
		return;

	SEISCOMP_DEBUG("%s: created picker %s",
	               rec->streamID().c_str(), _config.pickerType.c_str());

	pushProcessor(waveformID.networkCode(), waveformID.stationCode(),
	              waveformID.locationCode(), proc.get());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::emitPPick(const Processing::Picker *proc,
                    const Processing::Picker::Result &res,
                    double duration)
{
	PickMap::iterator it = _lastPicks.find(res.record->streamID());
	if ( it != _lastPicks.end() ) {
		if ( it->second->time().value() == res.time ) {
			SEISCOMP_WARNING("Duplicate pick on %s at %s: ignoring",
			                 res.record->streamID().c_str(),
			                 res.time.iso().c_str());
			return;
		}
	}

	DataModel::PickPtr pick;
	if ( _config.generateSimplifiedIDs ) {
		std::string pickID = res.time.toString("%Y%m%d.%H%M%S.%f-") + _config.pickerType +
		                     "-" + res.record->streamID();
		pick = DataModel::Pick::Create(pickID);

		if ( !pick ) {
			SEISCOMP_WARNING("Duplicate pick %s ignored", pickID.c_str());
			return;
		}
	}
	else {
		pick = DataModel::Pick::Create();

		if ( !pick ) {
			SEISCOMP_WARNING("Duplicate pick ignored");
			return;
		}
	}

	Core::Time now = Core::Time::UTC();
	DataModel::CreationInfo ci;
	ci.setCreationTime(now);
	ci.setAgencyID(agencyID());
	ci.setAuthor(author());
	pick->setCreationInfo(ci);

	if ( res.polarity ) {
		switch ( *res.polarity ) {
			case Processing::Picker::POSITIVE:
				pick->setPolarity(DataModel::PickPolarity(DataModel::POSITIVE));
				break;
			case Processing::Picker::NEGATIVE:
				pick->setPolarity(DataModel::PickPolarity(DataModel::NEGATIVE));
				break;
			case Processing::Picker::UNDECIDABLE:
				pick->setPolarity(DataModel::PickPolarity(DataModel::UNDECIDABLE));
				break;
			default:
				break;
		}
	}

	DataModel::TimeQuantity pickTime(res.time);
	if ( res.timeLowerUncertainty >= 0 && res.timeUpperUncertainty >= 0 &&
	     res.timeLowerUncertainty == res.timeUpperUncertainty )
		pickTime.setUncertainty(res.timeUpperUncertainty);
	else {
		if ( res.timeLowerUncertainty >= 0 )
			pickTime.setLowerUncertainty(res.timeLowerUncertainty);
		if ( res.timeUpperUncertainty >= 0 )
			pickTime.setUpperUncertainty(res.timeUpperUncertainty);
	}

	pick->setTime(pickTime);
	pick->setMethodID(proc->methodID());
	pick->setFilterID(proc->filterID());

	pick->setEvaluationMode(DataModel::EvaluationMode(DataModel::AUTOMATIC));

	pick->setPhaseHint(DataModel::Phase(_config.phaseHint));
	pick->setWaveformID(waveformStreamID(res.record));

	if ( _config.extraPickComments && res.snr >= 0 ) {
		DataModel::CommentPtr comment;

		if ( duration >= 0 ) {
			comment = new DataModel::Comment;
			comment->setId("duration");
			comment->setText(Core::toString(duration));
			pick->add(comment.get());
		}

		comment = new DataModel::Comment;
		comment->setId("SNR");
		comment->setText(Core::toString(res.snr));
		pick->add(comment.get());
	}

	if ( !_config.commentID.empty() && !_config.commentText.empty() ) {
		DataModel::CommentPtr comment;
		comment = new DataModel::Comment;
		comment->setId(_config.commentID);
		comment->setText(_config.commentText);
		pick->add(comment.get());
	}

	proc->finalizePick(pick.get());

	string phaseHintCode;
	try {
		phaseHintCode = pick->phaseHint().code();
	}
	catch ( ... ) {}

	bool isPrimary = Util::getShortPhaseName(phaseHintCode) == Util::getShortPhaseName(_config.phaseHint);

	SEISCOMP_DEBUG("Created %s'%s' pick %s", isPrimary ? "primary ":"", phaseHintCode, pick->publicID());

	_lastPicks[res.record->streamID()] = pick;

	DataModel::TimeWindow tw;
	tw.setReference(res.time);
	tw.setBegin(res.timeWindowBegin);
	tw.setEnd(res.timeWindowEnd);

	DataModel::AmplitudePtr amp;
	if ( _config.generateSimplifiedIDs ) {
		amp = DataModel::Amplitude::Create(pick->publicID() + ".snr");
	}
	else {
		amp = DataModel::Amplitude::Create();
	}

	if ( amp ) {
		amp->setCreationInfo(ci);

		amp->setPickID(pick->publicID());
		amp->setType("snr");

		amp->setWaveformID(pick->waveformID());
		amp->setTimeWindow(tw);

		amp->setSnr(res.snr);
		amp->setAmplitude(DataModel::RealQuantity(res.snr));

		SEISCOMP_DEBUG("Created %s amplitude %s", amp->type(), amp->publicID());
		SEISCOMP_DEBUG("  pickID   %s", amp->pickID());
	}

	if ( _config.featureExtractionType.empty() || !isPrimary
	  || !addFeatureExtractor(pick.get(), amp.get(), res.record, isPrimary) ) {
		sendPick(pick.get(), amp.get(), res.record, isPrimary);
		SEISCOMP_DEBUG("%s: emit %s pick %s", res.record->streamID(),
		               phaseHintCode, pick->publicID());
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::emitSPick(const Processing::SecondaryPicker *proc,
                    const Processing::SecondaryPicker::Result &res) {
	DataModel::PickPtr pick;

	if ( _config.generateSimplifiedIDs ) {
		std::string pickID = res.time.toString("%Y%m%d.%H%M%S.%f-") + _config.secondaryPickerType +
		                     "-" + res.record->streamID();
		pick = DataModel::Pick::Create(pickID);

		if ( !pick ) {
			SEISCOMP_WARNING("Duplicate pick %s ignored", pickID.c_str());
			return;
		}
	}
	else {
		pick = DataModel::Pick::Create();

		if ( !pick ) {
			SEISCOMP_WARNING("Duplicate pick ignored");
			return;
		}
	}

	Core::Time now = Core::Time::UTC();
	DataModel::CreationInfo ci;
	ci.setCreationTime(now);
	ci.setAgencyID(agencyID());
	ci.setAuthor(author());
	pick->setCreationInfo(ci);

	DataModel::TimeQuantity pickTime(res.time);
	if ( res.timeLowerUncertainty >= 0 && res.timeUpperUncertainty >= 0 &&
	     res.timeLowerUncertainty == res.timeUpperUncertainty )
		pickTime.setUncertainty(res.timeUpperUncertainty);
	else {
		if ( res.timeLowerUncertainty >= 0 )
			pickTime.setLowerUncertainty(res.timeLowerUncertainty);
		if ( res.timeUpperUncertainty >= 0 )
			pickTime.setUpperUncertainty(res.timeUpperUncertainty);
	}

	pick->setTime(pickTime);
	pick->setMethodID(proc->methodID());
	pick->setFilterID(proc->filterID());
	pick->setEvaluationMode(DataModel::EvaluationMode(DataModel::AUTOMATIC));
	pick->setPhaseHint(DataModel::Phase(res.phaseCode));
	pick->setWaveformID(waveformStreamID(res.record));

	DataModel::CommentPtr comment;
	if ( !proc->referencingPickID().empty() ) {
		comment = new DataModel::Comment;
		comment->setId("RefPickID");
		comment->setText(proc->referencingPickID());
		pick->add(comment.get());
	}

	if ( _config.extraPickComments && res.snr >= 0 ) {
		DataModel::CommentPtr comment;
		comment = new DataModel::Comment;
		comment->setId("SNR");
		comment->setText(Core::toString(res.snr));
		pick->add(comment.get());
	}

	if ( _config.featureExtractionType.empty()
	  || !addFeatureExtractor(pick.get(), NULL, res.record, false) ) {
		sendPick(pick.get(), NULL, res.record, false);
		SEISCOMP_DEBUG("%s: emit S pick %s", res.record->streamID().c_str(), pick->publicID().c_str());
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::emitDetection(const Processing::Detector *proc, const Record *rec, const Core::Time& time) {
	if ( !_config.pickerType.empty() ) {
		emitTrigger(proc, rec, time);

		if ( !_config.sendDetections ) return;
	}

	bool isDetection = !_config.pickerType.empty() && _config.sendDetections;
	Core::Time now = Core::Time::UTC();
	DataModel::PickPtr pick;
	if ( _config.generateSimplifiedIDs ) {
		pick = DataModel::Pick::Create(time.toString("%Y%m%d.%H%M%S.%f-") + rec->streamID());
	}
	else {
		pick = DataModel::Pick::Create();
	}

	DataModel::CreationInfo ci;
	ci.setCreationTime(now);
	ci.setAgencyID(agencyID());
	ci.setAuthor(author());
	pick->setCreationInfo(ci);
	pick->setTime(time);
	pick->setMethodID(proc->methodID());
	if ( !_config.commentID.empty() && !_config.commentText.empty() ) {
		DataModel::CommentPtr comment;
		comment = new DataModel::Comment;
		comment->setId(_config.commentID);
		comment->setText(_config.commentText);
		pick->add(comment.get());
	}

	// Set filterID
	string filter = _config.defaultFilter;

	const StreamConfig *sc = _stationConfig.get(&configuration(), configModuleName(),
	                                            rec->networkCode(), rec->stationCode());
	if ( sc )
		if ( !sc->filter.empty() ) filter = sc->filter;

	pick->setFilterID(filter);

	pick->setEvaluationMode(DataModel::EvaluationMode(DataModel::AUTOMATIC));
	if ( isDetection ) {
		// set the status to rejected if sendDections has been activated and the
		// repicker is active
		pick->setEvaluationStatus(DataModel::EvaluationStatus(DataModel::REJECTED));
	}
	pick->setPhaseHint(DataModel::Phase(_config.phaseHint));
	pick->setWaveformID(waveformStreamID(rec));

	if ( _config.extraPickComments ) {
		if ( static_cast<const Detector*>(proc)->duration() >= 0 ) {
			auto comment = new DataModel::Comment;
			comment->setId("duration");
			comment->setText(Core::toString(static_cast<const Detector*>(proc)->duration()));
			pick->add(comment);
		}
	}

	SEISCOMP_DEBUG("Created detection %s", pick->publicID().c_str());

	static_cast<const Detector*>(proc)->setPickID(pick->publicID());

	if ( _config.featureExtractionType.empty()
	  || !addFeatureExtractor(pick.get(), 0, rec, true) ) {
		sendPick(pick.get(), nullptr, rec, !isDetection);
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::emitFXPick(Seiscomp::DataModel::PickPtr pick,
                     DataModel::AmplitudePtr amp,
                     bool isPrimary,
                     const Processing::FX *proc,
	                 const Processing::FX::Result &res) {
	proc->finalizePick(pick.get());
	sendPick(pick.get(), amp.get(), res.record, isPrimary);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::sendPick(Seiscomp::DataModel::Pick *pick, DataModel::Amplitude *amp,
                   const Record *rec, bool isPrimary) {
#ifdef LOG_PICKS
	if ( !isMessagingEnabled() && !_ep ) {
		//cout << pick.get();
		printf("%s %-2s %-6s %-3s %-2s %6.1f %10.3f %4.1f %c %s\n",
		       pick->time().value().toString("%Y-%m-%d %H:%M:%S.%1f").c_str(),
		       pick->waveformID().networkCode().c_str(),
		       pick->waveformID().stationCode().c_str(),
		       pick->waveformID().channelCode().c_str(),
		       pick->waveformID().locationCode().empty()?"__":pick->waveformID().locationCode().c_str(),
		       -1.0, -1.0, -1.0, statusFlag(pick),
		       pick->publicID().c_str());
	}
#endif

	SEISCOMP_DEBUG("%s.%s.%s.%s: emit detection %s",
	               pick->waveformID().networkCode().c_str(),
	               pick->waveformID().stationCode().c_str(),
	               pick->waveformID().locationCode().c_str(),
	               pick->waveformID().channelCode().c_str(),
	               pick->publicID().c_str());
	Core::Time now = Core::Time::UTC();

	logObject(_logPicks, now);
	if ( amp )
		logObject(_logAmps, now);

	if ( connection() && !_config.test ) {
		DataModel::NotifierPtr n = new DataModel::Notifier("EventParameters", DataModel::OP_ADD, pick);
		DataModel::NotifierMessagePtr m = new DataModel::NotifierMessage;
		m->attach(n.get());

		for ( size_t i = 0; i < pick->commentCount(); ++i ) {
			n = new DataModel::Notifier(pick->publicID(), DataModel::OP_ADD, pick->comment(i));
			m->attach(n.get());
		}

		connection()->send(m.get());

		// Send amplitude
		if ( amp ) {
			n = new DataModel::Notifier("EventParameters", DataModel::OP_ADD, amp);
			m = new DataModel::NotifierMessage;
			m->attach(n.get());
			connection()->send(_config.amplitudeGroup, m.get());
		}
	}

	if ( _ep ) {
		_ep->add(pick);
		if ( amp )
			_ep->add(amp);
	}

	if ( isPrimary ) {
		if ( !_config.secondaryPickerType.empty() ) {
			addSecondaryPicker(pick->time().value(), rec, pick->publicID());
		}

		if ( _config.calculateAmplitudes ) {
			for ( StringSet::iterator it = _config.amplitudeList.begin();
			      it != _config.amplitudeList.end(); ++it ) {
				AmplitudeProcessorPtr proc = AmplitudeProcessorFactory::Create(it->c_str());
				if ( !proc ) continue;

				proc->setTrigger(pick->time().value());
				addAmplitudeProcessor(proc.get(), rec, pick);
			}
		}
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void App::emitAmplitude(const AmplitudeProcessor *ampProc,
                        const AmplitudeProcessor::Result &res) {

	if ( _config.dumpRecords && _config.offline )
		ampProc->writeData();

	bool update = true;
	DataModel::TimeWindow tw;
	tw.setReference(res.time.reference);
	tw.setBegin(res.time.begin);
	tw.setEnd(res.time.end);

	DataModel::AmplitudePtr amp = (DataModel::Amplitude*)ampProc->userData();
	Core::Time now = Core::Time::UTC();

	if ( !amp ) {
		if ( _config.generateSimplifiedIDs ) {
			amp = DataModel::Amplitude::Create(ampProc->referencingPickID() + "." + ampProc->type());
		}
		else {
			amp = DataModel::Amplitude::Create();
		}

		if ( !amp ) {
			SEISCOMP_WARNING("Internal error: duplicate amplitudeID?");
			return;
		}

		DataModel::CreationInfo ci;
		ci.setCreationTime(now);
		ci.setAgencyID(agencyID());
		ci.setAuthor(author());
		amp->setCreationInfo(ci);

		amp->setPickID(ampProc->referencingPickID());
		amp->setType(ampProc->type());
		amp->setWaveformID(waveformStreamID(res.record));
		ampProc->setUserData(amp.get());

		SEISCOMP_DEBUG("Created %s amplitude %s", amp->type().c_str(), amp->publicID().c_str());
		SEISCOMP_DEBUG("  pickID   %s", amp->pickID().c_str());

		update = false;
	}
	else {
		try {
			amp->creationInfo().setModificationTime(now);
		}
		catch ( Core::ValueException &e ) {
			DataModel::CreationInfo ci;
			ci.setModificationTime(now);
			amp->setCreationInfo(ci);
		}
	}

	amp->setUnit(ampProc->unit());
	amp->setTimeWindow(tw);
	if ( res.period > 0 ) amp->setPeriod(DataModel::RealQuantity(res.period));
	if ( res.snr >= 0 ) amp->setSnr(res.snr);
	amp->setAmplitude(
		DataModel::RealQuantity(
			res.amplitude.value, Core::None,
			res.amplitude.lowerUncertainty, res.amplitude.upperUncertainty,
			Core::None
		)
	);

	ampProc->finalizeAmplitude(amp.get());

#ifdef LOG_PICKS
	if ( !isMessagingEnabled() && !_ep ) {
		//cout << amp.get();
		if ( amp->type() == "snr" || amp->type() == "mb" ) {
			printf("%s %-2s %-6s %-3s %-2s %6.1f %10.3f %4.1f %c %s\n",
			       ampProc->trigger().toString("%Y-%m-%d %H:%M:%S.%1f").c_str(),
			       res.record->networkCode().c_str(), res.record->stationCode().c_str(),
			       res.record->channelCode().c_str(),
			       res.record->locationCode().empty()?"__":res.record->locationCode().c_str(),
			       amp->type() == "snr"?res.amplitude.value:-1.0, amp->type() == "mb"?res.amplitude.value:-1.0,
			       amp->type() == "mb"?res.period:-1.0, 'A',
			       amp->pickID().c_str());
		}
	}
#endif

	SEISCOMP_DEBUG("Emit amplitude %s, proc = 0x%lx, %s", amp->publicID().c_str(), (long int)ampProc, ampProc->type().c_str());
	logObject(_logAmps, now);

	if ( connection() && !_config.test ) {
		DataModel::NotifierPtr n = new DataModel::Notifier("EventParameters", update?DataModel::OP_UPDATE:DataModel::OP_ADD, amp.get());
		DataModel::NotifierMessagePtr m = new DataModel::NotifierMessage;
		m->attach(n.get());
		if ( !connection()->send(_config.amplitudeGroup, m.get()) && !update ) {
			ampProc->setUserData(NULL);
		}
	}

	if ( _ep )
		_ep->add(amp.get());
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
}
}
