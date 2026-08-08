#ifndef PROCESS_H
#define PROCESS_H

constexpr double PREDICTIVE_TIME = 4000.0; // time window for the prediction
// constexpr double PREDICTIVE_TIME_MS = 1000.0;

class Process {
  public:
    Process() = default;
    virtual ~Process() = default;
    // PRO-608: abstract interface base. Every concrete process is heap-allocated
    // and owned through Process* by Controller::startProcess(); none is ever
    // value-copied, and copying through the base would slice. Mirrors Plugin.h.
    Process(const Process &) = delete;
    Process &operator=(const Process &) = delete;
    Process(Process &&) = delete;
    Process &operator=(Process &&) = delete;

    virtual bool isRelayActive() = 0;

    virtual bool isAltRelayActive() = 0;

    virtual float getPumpValue() = 0;

    virtual void progress() = 0;

    virtual bool isActive() = 0;

    virtual bool isComplete() = 0;

    virtual int getType() = 0;

    virtual void updateVolume(double volume) = 0;
};

enum class ProcessTarget { VOLUMETRIC, TIME };
enum class ProcessPhase { RUNNING, FINISHED };

#endif // PROCESS_H
