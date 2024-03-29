################################################################################
# Copyright (C) 2014-2017 by GFZ Potsdam
#
# Classes to access an SDS structure to be used by the Dataselect-WS
#
# Author:  Javier Quinteros
# Email:   javier@gfz-potsdam.de
################################################################################

import datetime
import os

import seiscomp.logging
import seiscomp.mseedlite


class SDS:
    def __init__(self, sdsRoot):
        if isinstance(sdsRoot, list):
            self.sdsRoot = sdsRoot

        else:
            self.sdsRoot = [sdsRoot]

    def __getMSName(self, reqDate, net, sta, loc, cha):
        for root in self.sdsRoot:
            yield (
                f"{root}/{reqDate.year}/{net}/{sta}/{cha}.D/{net}.{sta}.{loc}.{cha}.D."
                f"{reqDate.year}.{reqDate.strftime('%j')}"
            )

    @staticmethod
    def __time2recno(msFile, reclen, timeStart, recStart, timeEnd, recEnd, searchTime):
        if searchTime <= timeStart:
            msFile.seek(recStart * reclen)
            rec = seiscomp.mseedlite.Record(msFile)
            return (recStart, rec.end_time)

        if searchTime >= timeEnd:
            msFile.seek(recEnd * reclen)
            rec = seiscomp.mseedlite.Record(msFile)
            return (recEnd, rec.end_time)

        t1 = timeStart
        r1 = recStart
        t2 = timeEnd
        r2 = recEnd
        rn = int(
            r1
            + (r2 - r1) * (searchTime - t1).total_seconds() / (t2 - t1).total_seconds()
        )

        rn = max(rn, recStart)
        rn = min(rn, recEnd)

        while True:
            msFile.seek(rn * reclen)
            rec = seiscomp.mseedlite.Record(msFile)

            if rec.begin_time < searchTime:
                r1 = rn
                t1 = rec.begin_time

                if t1 == t2:
                    break

                rn = int(
                    r1
                    + (r2 - r1)
                    * (searchTime - t1).total_seconds()
                    / (t2 - t1).total_seconds()
                )

                rn = max(rn, recStart)
                rn = min(rn, recEnd)

                if rn == r1:
                    break

            else:
                r2 = rn
                t2 = rec.begin_time

                if t1 == t2:
                    break

                rn = int(
                    r2
                    - (r2 - r1)
                    * (t2 - searchTime).total_seconds()
                    / (t2 - t1).total_seconds()
                )

                rn = max(rn, recStart)
                rn = min(rn, recEnd)

                if rn == r2:
                    break

        return rn, rec.end_time

    def __getWaveform(self, startt, endt, msFile, bufferSize):
        if startt >= endt:
            return

        rec = seiscomp.mseedlite.Record(msFile)
        reclen = rec.size
        recStart = 0
        timeStart = rec.begin_time

        if rec.begin_time >= endt:
            return

        msFile.seek(-reclen, 2)
        rec = seiscomp.mseedlite.Record(msFile)
        recEnd = msFile.tell() // reclen - 1
        timeEnd = rec.begin_time

        if rec.end_time <= startt:
            return

        if timeStart >= timeEnd:
            seiscomp.logging.error(
                f"{msFile.name}: overlap detected (start={timeStart}, end={timeEnd})"
            )
            return

        (lower, _) = self.__time2recno(
            msFile, reclen, timeStart, recStart, timeEnd, recEnd, startt
        )
        (upper, _) = self.__time2recno(
            msFile, reclen, startt, lower, timeEnd, recEnd, endt
        )

        if upper < lower:
            seiscomp.logging.error(
                f"{msFile.name}: overlap detected (lower={lower}, upper={upper})"
            )
            upper = lower

        msFile.seek(lower * reclen)
        remaining = (upper - lower + 1) * reclen
        check = True

        if bufferSize % reclen:
            bufferSize += reclen - bufferSize % reclen

        while remaining > 0:
            size = min(remaining, bufferSize)
            data = msFile.read(size)
            remaining -= size
            offset = 0

            if not data:
                return

            if check:
                while offset < len(data):
                    rec = seiscomp.mseedlite.Record(data[offset : offset + reclen])

                    if rec.begin_time >= endt:
                        return

                    if rec.end_time > startt:
                        break

                    offset += reclen

                check = False

            if offset < len(data):
                yield data[offset:] if offset else data

        while True:
            data = msFile.read(reclen)

            if not data:
                return

            rec = seiscomp.mseedlite.Record(data)

            if rec.begin_time >= endt:
                return

            yield data

    def __getDayRaw(self, day, startt, endt, net, sta, loc, cha, bufferSize):
        # Take into account the case of empty location
        if loc == "--":
            loc = ""

        for dataFile in self.__getMSName(day, net, sta, loc, cha):
            if not os.path.exists(dataFile):
                continue

            try:
                with open(dataFile, "rb") as msFile:
                    for buf in self.__getWaveform(startt, endt, msFile, bufferSize):
                        yield buf

            except seiscomp.mseedlite.MSeedError as e:
                seiscomp.logging.error(f"{dataFile}: {e}")

    def getRawBytes(self, startt, endt, net, sta, loc, cha, bufferSize):
        day = datetime.datetime(
            startt.year, startt.month, startt.day
        ) - datetime.timedelta(days=1)
        endDay = datetime.datetime(endt.year, endt.month, endt.day)

        while day <= endDay:
            for buf in self.__getDayRaw(
                day, startt, endt, net, sta, loc, cha, bufferSize
            ):
                yield buf

            day += datetime.timedelta(days=1)
