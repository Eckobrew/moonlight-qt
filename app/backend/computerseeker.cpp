#include "computerseeker.h"
#include "computermanager.h"
#include <QTimer>

ComputerSeeker::ComputerSeeker(ComputerManager *manager, QString computerName, QObject *parent)
    : QObject(parent), m_ComputerManager(manager), m_ComputerName(computerName),
      m_TimeoutTimer(new QTimer(this)),
      m_WolTimer(new QTimer(this))
{
    m_TimeoutTimer->setSingleShot(true);
    connect(m_TimeoutTimer, &QTimer::timeout,
            this, &ComputerSeeker::onTimeout);
    connect(m_ComputerManager, &ComputerManager::computerStateChanged,
            this, &ComputerSeeker::onComputerUpdated);
    connect(m_WolTimer, &QTimer::timeout,
            this, &ComputerSeeker::onWolTimer);

    // If we know this computer, send a WOL packet to wake it up in case it is asleep.
    NvComputer* matchingComputer = findMatchingComputer();
    if (matchingComputer) {
        matchingComputer->wake();
        if (matchingComputer->state != NvComputer::CS_ONLINE) {
            m_WolTimer->start(2500);
        }
    }
}

void ComputerSeeker::onWolTimer()
{
    NvComputer* matchingComputer = findMatchingComputer();
    if (matchingComputer && matchingComputer->state != NvComputer::CS_ONLINE) {
        matchingComputer->wake();
    } else if (matchingComputer && isOnline(matchingComputer)) {
        m_WolTimer->stop();
    }
}

void ComputerSeeker::start(int timeout)
{
    NvComputer* matchingComputer = findMatchingComputer();
    if (timeout <= 0) {
        timeout = (matchingComputer && matchingComputer->state != NvComputer::CS_ONLINE) ? 60000 : 30000;
    }
    m_TimeoutTimer->start(timeout);

    if (matchingComputer && matchingComputer->state != NvComputer::CS_ONLINE) {
        emit wakingComputer();
    }

    // If we don't know this computer by name, address, or UUID, try adding it
    // manually and see if we can find it by address, hostname, or mDNS.
    //
    // NB: We don't do this unconditionally because it will wipe out the user's
    // manual address if they pass another reachable hostname/address.
    if (!matchingComputer) {
        m_ComputerManager->addNewHostManually(m_ComputerName);
    }

    m_ComputerManager->startPolling();
}

void ComputerSeeker::onComputerUpdated(NvComputer *computer)
{
    if (!m_TimeoutTimer->isActive()) {
        return;
    }
    if (matchComputer(computer) && isOnline(computer)) {
        m_WolTimer->stop();
        m_ComputerManager->stopPollingAsync();
        m_TimeoutTimer->stop();
        emit computerFound(computer);
    }
}

bool ComputerSeeker::matchComputer(NvComputer *computer) const
{
    QString value = m_ComputerName.toLower();

    if (computer->name.toLower() == value || computer->uuid.toLower() == value) {
        return true;
    }

    const auto uniqueAddresses = computer->uniqueAddresses();
    for (const NvAddress& addr : uniqueAddresses) {
        if (addr.address().toLower() == value || addr.toString().toLower() == value) {
            return true;
        }
    }

    return false;
}

NvComputer* ComputerSeeker::findMatchingComputer() const
{
    const auto computers = m_ComputerManager->getComputers();
    for (NvComputer* computer : computers) {
        if (this->matchComputer(computer)) {
            return computer;
        }
    }

    return nullptr;
}

bool ComputerSeeker::isOnline(NvComputer *computer) const
{
    return computer->state == NvComputer::CS_ONLINE;
}

void ComputerSeeker::onTimeout()
{
    m_WolTimer->stop();
    m_TimeoutTimer->stop();
    m_ComputerManager->stopPollingAsync();
    emit errorTimeout();
}
