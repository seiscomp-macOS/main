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


#define SEISCOMP_COMPONENT QcTool
#include <seiscomp/logging/log.h>

#include <seiscomp/core/datetime.h>
#include <seiscomp/client/inventory.h>
#include <seiscomp/client/configdb.h>
#include <seiscomp/processing/application.h>
#include <seiscomp/datamodel/config_package.h>
#include <seiscomp/datamodel/utils.h>
#include <seiscomp/core/baseobject.h>

#include <boost/any.hpp>
#include <boost/regex.hpp>

#include "qctool.h"


using boost::any_cast;
using namespace std;
using namespace Seiscomp::Client;
using namespace Seiscomp::Math;


namespace Seiscomp {
namespace Applications {
namespace Qc {
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QcTool::QcTool(int argc, char **argv)
: QcApp(argc, argv) {
	setLoadInventoryEnabled(false);
	setLoadStationsEnabled(true);
	setLoadConfigModuleEnabled(true);

	setPrimaryMessagingGroup("QC");
	addMessagingSubscription("CONFIG");

	DataModel::Notifier::Disable();

	_qcMessenger = new QcMessenger(this);

	//! TODO
	_ringBufferSize = 5. * 60.;
	_leadTime = 0. * 60.;
	_maxGapLength = 1. * 60.;
	_archiveMode = false;
	_autoTime = false;

	_use3Components = false;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QcTool::~QcTool() {}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool QcTool::archiveMode() const {
	return _archiveMode;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
string QcTool::creatorID() const {
	return _creator;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::createCommandLineDescription() {
	StreamApplication::createCommandLineDescription();

	commandline().addOption("Messaging", "test", "Disable sending messages");
	commandline().addGroup("Archive-Processing");
	commandline().addOption("Archive-Processing", "archive", "Processing of archived data.");
	commandline().addOption("Archive-Processing", "auto-time", "Automatic determination of start time for each stream from last db entries.\nend-time is set to future.");
	commandline().addOption("Archive-Processing", "begin-time", "Begin time of record acquisition.\n[e.g.: \"2008-11-11 10:33:50\"]", (string*)nullptr);
	commandline().addOption("Archive-Processing", "end-time", "End time of record acquisition. If unset, current Time is used.", (string*)nullptr);
	commandline().addOption("Archive-Processing", "stream-mask", "Use this regexp for stream selection.\n[e.g. \"^GE.*BHZ$\"]", (string*)nullptr);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool QcTool::validateParameters() {
	if ( !StreamApplication::validateParameters() ) {
		return false;
	}

	if ( (_archiveMode = commandline().hasOption("archive")) ) {
		if ( !(_autoTime = commandline().hasOption("auto-time")) ) {
			try {
				string begin = commandline().option<string>("begin-time");
				cout << "begin-time = " << begin << endl;
				_beginTime = Seiscomp::Core::Time::FromString(begin.c_str(), "%F %T");
			}
			catch ( ... ) {
				_beginTime = Core::Time::UTC() - Core::TimeSpan(24 * 3600, 0);
				SEISCOMP_WARNING("using (current time - 24h) as begin time");
			}
		}

		try {
			string end = commandline().option<string>("end-time");
			cout << "end-time = " << end << endl;
			_endTime = Seiscomp::Core::Time::FromString(end.c_str(), "%F %T");
		}
		catch ( ... ) {
			_endTime = Core::Time::UTC();
			SEISCOMP_WARNING("using current time as end time");
		}

	}

	try {
		_streamMask = commandline().option<string>("stream-mask");
		cout << "stream-mask = " << _streamMask << endl;
		_useConfiguredStreams = false;
		setLoadConfigModuleEnabled(false);
	}
	catch ( ... ) {}

	if ( commandline().hasOption("test") ) {
		_qcMessenger->setTestMode(true);
	}

	//setMessagingEnabled(!commandline().hasOption("archive"));
	//setDatabaseEnabled(!commandline().hasOption("archive"), true);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool QcTool::initConfiguration() {
	if ( !Processing::Application::initConfiguration() )
		return false;

	try {
		_useConfiguredStreams = configGetBool("useConfiguredStreams");
	}
	catch ( ... ) {
		_useConfiguredStreams = true;
	}

	try {
		_use3Components = configGetBool("use3Components");
	}
	catch ( ... ) {
		_use3Components = false;
	}

	if ( !_useConfiguredStreams ) {
		try {
			_streamMask = configGetString("streamMask");
		}
		catch ( ... ) {
			_streamMask = "^.*BHZ$";
		}
	}

	try {
		_creator = configGetString("CreatorId");
	}
	catch ( ... ) {
		_creator = "SC3_QcTool";
	}

	try {
		_dbLookBack = boost::lexical_cast<int>(configGetString("dbLookBack"));
	}
	catch ( ... ) {
		_dbLookBack = 7;
	}

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool QcTool::init() {
	if ( !StreamApplication::init() ) {
		return false;
	}

	QcPluginPtr qcPlugin;
	QcPluginFactory::ServiceNames* services = QcPluginFactory::Services();

	if ( services ) {
		for (QcPluginFactory::ServiceNames::iterator it = services->begin(); it != services->end(); ++it ) {
			qcPlugin = QcPlugin::Cast(QcPluginFactory::Create(it->c_str()));
			if (!qcPlugin) {
				SEISCOMP_WARNING("QcPlugin %s not found!", it->c_str());
				continue;
			}

			vector<string> names = qcPlugin->parameterNames();
			_allParameterNames.insert(names.begin(),names.end());

			// test, if this plugin is able to process archived data
			if (_archiveMode && QcConfig::RealtimeOnly(this,*it))
				SEISCOMP_INFO("%s plugin is only for realtime processing. Skipped.", it->c_str());
			else {
				if (!_plugins[*it])
					_plugins[*it] = new QcConfig(this,*it);
			}
		}
		delete services;
	}
	else {
		SEISCOMP_ERROR("-------------------------------------------------------------");
		SEISCOMP_ERROR(" ");
		SEISCOMP_ERROR("ERROR! No QcPlugins found!");
		SEISCOMP_ERROR("In order to use this program,");
		SEISCOMP_ERROR("you have to specify the QcPlugins in");
		SEISCOMP_ERROR("the config file!");
		SEISCOMP_ERROR(" ");
		SEISCOMP_ERROR(" ");
		SEISCOMP_ERROR("-------------------------------------------------------------");
		cerr << "giving up! Exiting... try: --debug" << endl;
		return false;
	}

	streamBuffer().setTimeSpan(_ringBufferSize);

	if (_archiveMode) {
		SEISCOMP_INFO("*** ARCHIVE MODE ***");
	}
	else {
		_beginTime = Core::Time::UTC() - Core::TimeSpan(_leadTime);
		_endTime = Core::None;
	}

	if (!_streamMask.empty())
		_useConfiguredStreams = false;

	if ( isDatabaseEnabled() ) {
		if ( _useConfiguredStreams ) {
			SEISCOMP_DEBUG("Reading configured streams:");

			DataModel::ConfigModule* module = configModule();

			if ( module ) {
				for ( size_t j = 0; j < module->configStationCount(); ++j ) {
					if ( _exitRequested ) break;

					DataModel::ConfigStation* station = module->configStation(j);
					DataModel::Setup *setup = DataModel::findSetup(station, name());

					if ( setup ) {
						DataModel::ParameterSet* ps = DataModel::ParameterSet::Find(setup->parameterSetID());
						if ( !ps ) {
							SEISCOMP_ERROR("Cannot find parameter set %s", setup->parameterSetID().c_str());
							continue;
						}

						Util::KeyValues keys;
						keys.init(ps);

						string net, sta, loc, cha;
						net = station->networkCode();
						sta = station->stationCode();

						keys.getString(loc, "detecLocid");
						keys.getString(cha, "detecStream");

						if ( !cha.empty() ) {
							bool isFixedChannel = cha.size() > 2;

							DataModel::SensorLocation *sloc =
								Client::Inventory::Instance()->getSensorLocation(net, sta, loc, Core::Time::UTC());

							if ( _use3Components ) {
								std::string groupCode;

								if ( isFixedChannel )
									groupCode = cha.substr(0, 2);
								else
									groupCode = cha;

								if ( sloc ) {
									DataModel::ThreeComponents tc;

									if ( DataModel::getThreeComponents(tc, sloc, groupCode.c_str(), Core::Time::UTC()) ) {
										if ( tc.comps[DataModel::ThreeComponents::Vertical] )
											addStream(net, sta, loc, tc.comps[DataModel::ThreeComponents::Vertical]->code());
										else
											addStream(net, sta, loc, isFixedChannel ? cha : groupCode + 'Z');

										if ( tc.comps[DataModel::ThreeComponents::FirstHorizontal] )
											addStream(net, sta, loc, tc.comps[DataModel::ThreeComponents::FirstHorizontal]->code());
										else
											addStream(net, sta, loc, groupCode+'N');

										if ( tc.comps[DataModel::ThreeComponents::SecondHorizontal] )
											addStream(net, sta, loc, tc.comps[DataModel::ThreeComponents::SecondHorizontal]->code());
										else
											addStream(net, sta, loc, groupCode+'E');
									}
									else {
										addStream(net, sta, loc, isFixedChannel ? cha : groupCode + 'Z');
										addStream(net, sta, loc, groupCode+'N');
										addStream(net, sta, loc, groupCode+'E');
									}
								}
								else {
									addStream(net, sta, loc, isFixedChannel ? cha : groupCode + 'Z');
									addStream(net, sta, loc, groupCode+'N');
									addStream(net, sta, loc, groupCode+'E');
								}
							}
							else {
								// Only vertical
								if ( !isFixedChannel ) {
									if ( sloc ) {
										DataModel::Stream *stream = DataModel::getVerticalComponent(sloc, cha.c_str(), Core::Time::UTC());
										if ( stream )
											addStream(net, sta, loc, stream->code());
										else
											addStream(net, sta, loc, cha+'Z');
									}
									else
										addStream(net, sta, loc, cha+'Z');
								}
								else
									addStream(net, sta, loc, cha);
							}
						}
					}
				}
			}

			if ( _streamIDs.empty() )
				SEISCOMP_WARNING("[empty]");
		}
		else { //! read all matching streams...
			string mask = _streamMask;
			boost::smatch what;
			try {
				boost::regex streamMask(mask);
				SEISCOMP_DEBUG("Reading those streams matching: %s", mask.c_str());

				DataModel::Inventory* inv = Seiscomp::Client::Inventory::Instance()->inventory();
				if ( inv ) {
					for ( size_t i = 0; i < inv->networkCount(); ++i ) {
						DataModel::NetworkPtr network = inv->network(i);

						for ( size_t j = 0; j < network->stationCount(); ++j ) {
							DataModel::StationPtr station = network->station(j);
							try {
								station->end();
								continue;
							}
							catch (...) {}

							for ( size_t l = 0; l < station->sensorLocationCount(); ++l ) {
								DataModel::SensorLocationPtr sloc = station->sensorLocation(l);
								try {
									sloc->end();
									continue;
								}
								catch (...) {}

								for ( size_t s = 0; s < sloc->streamCount(); ++s ) {
									DataModel::StreamPtr stream = sloc->stream(s);

									string net, sta, loc, cha;
									net = network->code();
									sta = station->code();
									loc = sloc->code();
									cha = stream->code();

									string streamID = net + "." + sta + "." + loc + "." + cha;
									if ( !boost::regex_match(streamID, what, streamMask) ) {
										SEISCOMP_DEBUG("ignoring: %s", streamID.c_str());
										continue;
									}

									addStream(net, sta, loc, cha);
								}
							}
						}
					}
				}
			}
			catch ( std::exception &e ) {
				SEISCOMP_ERROR("Exception: %s", e.what());
				return false;
			}
		}
	}

	SEISCOMP_DEBUG("number of streams: %ld", (long int)_streamIDs.size());

	// Enable timeout callback every second
	enableTimer(1);

	return true;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//! Add stream for processing.
void QcTool::addStream(string net, string sta, string loc, string cha) {
	string streamID = net + "." + sta + "." + loc + "." + cha;
	_streamIDs.insert(streamID);

	Core::Time begin = _beginTime;

	if ( _autoTime ) {
		begin = findLast(net, sta, loc, cha);
	}

	SEISCOMP_INFO("adding stream: %s with timewindow: %s -- %s",
	              streamID, begin.iso(), _endTime ? _endTime->iso() : "[]");
	recordStream()->addStream(net, sta, loc, cha, begin, _endTime);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
//! Query database for last WaveformQuality entries for a given streamID.
Core::Time QcTool::findLast(string net, string sta, string loc, string cha) {
	SEISCOMP_INFO("begin time auto-detection...");

	DatabaseIterator dbit = query()->getWaveformQualityDescending(WaveformStreamID(net, sta, loc, cha, ""), "rms", "report");
	DataModel::WaveformQuality* wfq;
	int year, month, day;
	Core::Time::UTC().get(&year, &month, &day);
	Core::Time today(year, month, day);
	Core::Time lastEndTime = today - Core::TimeSpan(_dbLookBack * 24 * 3600.0);
	if ( dbit.valid() ) {
		wfq = DataModel::WaveformQuality::Cast(dbit.get());
		try {
			lastEndTime = wfq->end();
		}
		catch(...) {
			SEISCOMP_DEBUG("auto-detection: no valid end time found!");
		}
	}
	else {
		std::string streamID(net+"."+sta+"."+loc+"."+cha);
		SEISCOMP_WARNING("no last database entry found for stream: %s", streamID.c_str());
	}
	dbit.close();

	string streamID = net + "." + sta + "." + loc + "." + cha;
	SEISCOMP_INFO("[%s] using %s as begin time", streamID.c_str(), lastEndTime.iso().c_str());
	return lastEndTime;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::done() {
	//! trigger QcPlugins to make last calculation before finish
	doneSignal();

	//! flush messages
	_qcMessenger->flushMessages();
	Processing::Application::done();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::initQc(const string &networkCode, const string &stationCode,
                    const string &locationCode, const string &channelCode) {

	QcPlugin *qcPlugin;

	const string streamID = networkCode + "." + stationCode  + "." + locationCode  + "." + channelCode;

	for ( auto it = _plugins.begin(); it != _plugins.end(); ++it ) {
		qcPlugin = QcPlugin::Cast(QcPluginFactory::Create(it->first.c_str()));
		if ( !qcPlugin ) {
			SEISCOMP_WARNING("QcPlugin %s not found!", it->first);
			continue;
		}

		if ( !qcPlugin->init(this, it->second.get(), streamID) ) {
			SEISCOMP_WARNING("Initializing QcPlugin %s failed! Skipped.",
			                 qcPlugin->registeredName());
			delete qcPlugin;
			continue;
		}

		_qcPluginMap.insert(pair<string, QcPluginCPtr>(streamID, qcPlugin));
		addProcessor(networkCode, stationCode, locationCode, channelCode, qcPlugin->qcProcessor());
	}

	if ( _plugins.size() > 0 ) {
		SEISCOMP_DEBUG("number of streams: %ld", static_cast<size_t>(_qcPluginMap.size() / _plugins.size()));
	}
	else {
		SEISCOMP_ERROR("no QC plugins loaded!");
	}
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
QcMessenger *QcTool::qcMessenger() const {
	return _qcMessenger.get();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
bool QcTool::exitRequested() const {
	return _exitRequested;
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::handleNewStream(const Record *rec) {
	if ( _streamIDs.find(rec->streamID()) != _streamIDs.end() ) {
		initQc(rec->networkCode(),rec->stationCode(),rec->locationCode(),rec->channelCode());
	}

}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::processorFinished(const Record *rec, Processing::WaveformProcessor *wp) {
	// TODO: detachQcPlugin(wp);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::handleTimeout() {
	_emitTimeout();
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
void QcTool::addTimeout(const TimerSignal::slot_type& onTimeout) const {
	_emitTimeout.connect(onTimeout);
}
// <<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<<




// >>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>>
}
}
}
