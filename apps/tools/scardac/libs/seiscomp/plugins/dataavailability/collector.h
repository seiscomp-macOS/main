/***************************************************************************
 * Copyright (C) 2020 by gempa GmbH                                        *
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

#ifndef SEISCOMP_DATAAVAILABILITY_COLLECTOR_H
#define SEISCOMP_DATAAVAILABILITY_COLLECTOR_H

#include <seiscomp/plugins/dataavailability/api.h>

#include <seiscomp/core/exceptions.h>
#include <seiscomp/core/interfacefactory.h>
#include <seiscomp/core/interruptible.h>
#include <seiscomp/core/optional.h>
#include <seiscomp/core/timewindow.h>

#include <seiscomp/datamodel/dataextent.h>
#include <seiscomp/datamodel/datasegment.h>
#include <seiscomp/datamodel/waveformstreamid.h>

#include <map>
#include <vector>

/******************************************************************************
 scardac API Changelog
 ******************************************************************************

 1
   - Initial API

 2
   - Add setStartTime() and setEndTime() methods

 */
#define SCARDAC_API_VERSION 2


namespace Seiscomp {
namespace DataAvailability {

class CollectorException : public Core::GeneralException {
	public:
		CollectorException();
		CollectorException(std::string what);
};


DEFINE_SMARTPOINTER(Collector);

/**
 * @brief The Collector class defines an abstract interface to read record meta
 * data of a waveform archive, e.g., data files organized in a directory
 * structure.
 */
class SC_DAPLUGIN_API Collector : public Core::InterruptibleObject {

	// ------------------------------------------------------------------
	//  Data file iterator
	// ------------------------------------------------------------------
	public:
		DEFINE_SMARTPOINTER(RecordIterator);

		class RecordIterator : public Seiscomp::Core::InterruptibleObject {
			protected:
				RecordIterator() = default;

			public:
				virtual ~RecordIterator() = default;

				virtual bool valid() const = 0;
				virtual bool next() = 0;
				virtual const Core::Time& startTime() const = 0;
				virtual const Core::Time& endTime() const = 0;
				virtual double sampleRate() const = 0;
				virtual const std::string& quality() const = 0;

				void handleInterrupt(int sig) override;

			protected:
				bool _abortRequested{false};
		};


	DECLARE_SC_CLASS(Collector)

	// ------------------------------------------------------------------
	//  X'truction
	// ------------------------------------------------------------------
	protected:
		/**
		 * @brief The constructor is protected because this is an
		 *        abstract base class.
		 */
		Collector() = default;


	public:
		//! D'tor
		virtual ~Collector() = default;


	// ------------------------------------------------------------------
	//  Collector interface
	// ------------------------------------------------------------------
	public:
		using WaveformIDs = std::map<std::string, DataModel::WaveformStreamID>;
		using DataChunks = std::vector<std::string>;

		/**
		 * @brief Set the source location of the waveform archive. This is
		 *        implementation specific but typically the base path of a
		 *        directory structure.
		 * @param source The source definition.
		 */
		virtual bool setSource(const char *source);

		/**
		 * @brief Set a start time for all collection routines.
		 * @param startTime Start of timewindow to restrict collection operation
		 *        to.
		 * @since SCARDAC API 2, SeisComP ABI 16.0.0
		 */
		virtual void setStartTime(Core::Time startTime);

		/**
		 * @brief Set an end time for all collection routines.
		 * @param endTime End of timewindow to restrict collection operation to.
		 * @since SCARDAC API 2, SeisComP ABI 16.0.0
		 */
		virtual void setEndTime(Core::Time endTime);

		/**
		 * @brief Reset all internal buffers and states except for the source.
		 */
		virtual void reset();

		/**
		 * @brief Collect a set of waveform IDs found in the archive.
		 * @param wids Result set containing the collected waveform IDs.
		 * @return Status flag.
		 */
		virtual void collectWaveformIDs(WaveformIDs &wids) = 0;

		/**
		 * @brief Collect data chucks, e.g., file names, for a spefific
		 *        waveform ID in the specified time window. The chunks must be
		 *        sorted according to the sampling time in ascending order.
		 * @param chunks A vector to store the chunk IDs, e.g., the file names
		 *        relative to the archive base path.
		 * @param wid The stream ID to look up chunks for.
		 */
		virtual void collectChunks(DataChunks &chunks,
		                           const DataModel::WaveformStreamID &wid) = 0;

		/**
		 * @brief Return the data time window for a specific data chunk. The
		 *        returned time window is not expected to guarantee the
		 *        availability of data.
		 * @param window The time window represented by the data chunk
		 * @param chunk The chunk ID, e.g., the file name relative to the
		 *        archive base path.
		 * @return Status flag.
		 */
		virtual bool chunkTimeWindow(Core::TimeWindow &window,
		                             const std::string &chunk) = 0;

		/**
		 * @brief Return the chunk's last modification time.
		 * @param chunk The chunk ID, e.g., the file name relative to the
		 *        archive base path.
		 * @return Chunk modification time.
		 */
		virtual Core::Time chunkMTime(const std::string &chunk);


		/**
		 * @brief Open a data chunk for record-based iteration.
		 * @param chunk The chunk ID, e.g., the file name relative to the
		 *        archive base path.
		 * @param wid The waveform stream ID corresponding to the chunk
		 * @return The data chunk iterator or nullptr if the chunk could not be
		 *         opened. The ownership is transferred to the caller.
		 */
		virtual RecordIterator* begin(const std::string &chunk,
	                                  const DataModel::WaveformStreamID &wid) = 0;

		/**
		 * @brief threadSafe Define whether or not a collector instance may
		 * be used by multiple threads simultaneously. Thread-safty is only
		 * required for the following methods:
		 *  - chunkTimeWindow
		 *  - chunkMTime
		 *  - begin
		 * @return True if the collector instance can be used from multiple
		 * threads.
		 */
		virtual bool threadSafe() const = 0;


	// ------------------------------------------------------------------
	//  Collector static interface
	// ------------------------------------------------------------------
	public:
		/**
		 * @brief Create a data availability collector for the given service.
		 * @param service The service name.
		 * @return A pointer to the data availabiltiy collector object
		 *         \note
		 *         The returned pointer has to be deleted by the caller!
		 */
		static Collector *Create(const char *service);

		/**
		 * @brief Open a data availability collector. This will call @Create.
		 * @param url A source URL of format [service://]source,
		 *        e.g., sds:///data/archive. Service defaults to 'sds'.
		 * @return A pointer to the data availabiltiy collector object. If the
		 *         requested service is not supported, nullptr will be returned.
		 *         \note
		 *         The returned pointer has to be deleted by the caller!
		 */
		static Collector *Open(const char *url);

	// ------------------------------------------------------------------
	//  Collector protected interface
	// ------------------------------------------------------------------
	protected:

		void handleInterrupt(int) override;

	// ------------------------------------------------------------------
	//  Protected members
	// ------------------------------------------------------------------
	protected:
		std::string     _source;
		bool            _abortRequested{false};

		OPT(Core::Time) _startTime;
		OPT(Core::Time) _endTime;
};


DEFINE_INTERFACE_FACTORY(Collector);


#define REGISTER_DATAAVAILABILITY_COLLECTOR_VAR(Class, Service) \
Seiscomp::Core::Generic::InterfaceFactory<Seiscomp::DataAvailability::Collector, Class> __##Class##InterfaceFactory__(Service)

#define REGISTER_DATAAVAILABILITY_COLLECTOR(Class, Service) \
static REGISTER_DATAAVAILABILITY_COLLECTOR_VAR(Class, Service)


} // ns DataAvailability
} // ns Seiscomp


#endif // SEISCOMP_DATAAVAILABILITY_COLLECTOR_H
