#include "pch.h"
#include "ProcessHistory.h"

ProcessHistorySample ProcessHistory::Compute(ProcessCounters const& now, ProcessCounters const* prev, int processors, int64_t frequency) {
	ProcessHistorySample s;
	s.Private = (float)now.Private;
	s.WorkingSet = (float)now.WorkingSet;
	if (!prev || now.Time <= prev->Time || frequency <= 0 || processors <= 0)
		return s;
	double seconds = double(now.Time - prev->Time) / frequency;
	// the CPU times are in 100 ns units; percent of all processors
	double total = seconds * 10'000'000 * processors / 100;
	s.HasRates = true;
	s.KernelCpu = float((now.KernelTime - prev->KernelTime) / total);
	s.UserCpu = float((now.UserTime - prev->UserTime) / total);
	s.Read = float((now.Read - prev->Read) / seconds);
	s.Write = float((now.Write - prev->Write) / seconds);
	s.Other = float((now.Other - prev->Other) / seconds);
	if (now.HasNetwork && prev->HasNetwork) {
		// a restarted tracker counts from zero again
		s.HasNetwork = true;
		s.Received = now.Received >= prev->Received ? float((now.Received - prev->Received) / seconds) : 0;
		s.Sent = now.Sent >= prev->Sent ? float((now.Sent - prev->Sent) / seconds) : 0;
	}
	return s;
}

void ProcessHistory::Push(ProcessCounters const& now, int processors, int64_t frequency) {
	m_Samples.push_back(Compute(now, m_Last ? &*m_Last : nullptr, processors, frequency));
	if (m_Samples.size() > Capacity)
		m_Samples.pop_front();
	m_Last = now;
}
