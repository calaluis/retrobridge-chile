#include "rb/cassette/fsk_loader.hpp"

namespace rb::cassette {

namespace {

uint16_t le16(const uint8_t* p) noexcept
{
    return static_cast<uint16_t>(p[0] | (p[1] << 8));
}

}  // namespace

void FskLoader::bind(rb::core::Span<uint8_t> bank) noexcept
{
    bank_ = bank;
    source_ = nullptr;
    published_.store(0, std::memory_order_release);
    setState(LoaderState::Idle);
}

bool FskLoader::start(TapeSource& source, const CasSegment& run, uint64_t runwayUs,
                      uint32_t prefillValues) noexcept
{
    if (run.kind != SegmentKind::Fsk || run.chunks == 0 || run.length > bank_.size()) {
        return false;
    }
    source_ = &source;
    run_ = run;
    runwayUs_ = runwayUs;
    prefillValues_ = prefillValues;
    chunksDone_ = 0;
    chunkPos_ = run.offset;
    chunkLeft_ = 0;
    inPayload_ = false;
    loaded_ = 0;
    budget_ = kFaultBudget;
    reads_ = 0;
    faults_ = 0;
    stopRequested_.store(false, std::memory_order_relaxed);
    published_.store(0, std::memory_order_release);
    setState(LoaderState::Loading);
    return true;
}

LoaderStep FskLoader::step() noexcept
{
    const LoaderState now = state();
    if (now == LoaderState::Final) {
        return LoaderStep::Done;
    }
    if (now != LoaderState::Loading) {
        return now == LoaderState::Stopped ? LoaderStep::Stopped : LoaderStep::Failed;
    }
    if (stopRequested_.load(std::memory_order_acquire)) {
        setState(LoaderState::Stopped);
        return LoaderStep::Stopped;
    }

    if (!inPayload_) {
        if (chunksDone_ == run_.chunks) {
            if (loaded_ != run_.length) {
                setState(LoaderState::Failed);  // the file no longer matches the index
                return LoaderStep::Failed;
            }
            setState(LoaderState::Final);  // values were published before the state
            return LoaderStep::Done;
        }
        uint8_t header[CasIndex::kHeaderSize];
        ++reads_;
        if (!source_->read(chunkPos_, rb::core::Span<uint8_t>(header))) {
            return fault();
        }
        budget_ = kFaultBudget;
        const uint16_t length = le16(header + 4);
        const bool isFsk = header[0] == 'f' && header[1] == 's' && header[2] == 'k' && header[3] == ' ';
        const bool joins = chunksDone_ == 0 || le16(header + 6) == 0;
        if (!isFsk || !joins || (length & 1u) != 0 || length > run_.length - loaded_) {
            setState(LoaderState::Failed);  // the file no longer matches the index
            return LoaderStep::Failed;
        }
        chunkPos_ += CasIndex::kHeaderSize;
        chunkLeft_ = length;
        inPayload_ = true;
        if (chunkLeft_ > 0) {
            return LoaderStep::Progress;
        }
    }

    if (chunkLeft_ > 0) {
        // One read: up to the end of the current bank block, and not past this chunk.
        const uint32_t toBlockEnd = kBlockBytes - (loaded_ % kBlockBytes);
        const uint32_t n = chunkLeft_ < toBlockEnd ? chunkLeft_ : toBlockEnd;
        ++reads_;
        if (!source_->read(chunkPos_, bank_.subspan(loaded_, n))) {
            return fault();
        }
        budget_ = kFaultBudget;
        chunkPos_ += n;
        chunkLeft_ -= n;
        loaded_ += n;
        published_.store(loaded_ / 2u, std::memory_order_release);  // whole values only: lengths are even
    }
    if (chunkLeft_ == 0) {
        inPayload_ = false;
        ++chunksDone_;
    }
    return LoaderStep::Progress;
}

LoaderStep FskLoader::fault() noexcept
{
    ++faults_;
    if (budget_ > 0) {
        --budget_;
        return LoaderStep::Progress;  // the cursor did not move: the same read is asked again
    }
    setState(LoaderState::Failed);  // the 4th failure in a row; what was published stays valid
    return LoaderStep::Failed;
}

void FskLoader::requestStop() noexcept
{
    stopRequested_.store(true, std::memory_order_release);
}

LoaderState FskLoader::state() const noexcept
{
    return static_cast<LoaderState>(state_.load(std::memory_order_acquire));
}

uint32_t FskLoader::published() const noexcept
{
    return published_.load(std::memory_order_acquire);
}

ValueFetch FskLoader::next(uint32_t valueIndex, uint16_t& value) const noexcept
{
    uint32_t ready = published();
    if (valueIndex >= ready) {
        // Values are published before the state: once Final is seen, the count is final.
        if (state() != LoaderState::Final) {
            return ValueFetch::Underrun;
        }
        ready = published();
        if (valueIndex >= ready) {
            return ValueFetch::End;
        }
    }
    value = le16(bank_.data() + static_cast<std::size_t>(valueIndex) * 2u);
    return ValueFetch::Value;
}

rb::core::Span<const uint8_t> FskLoader::piece(uint32_t valueIndex) const noexcept
{
    const uint32_t ready = published();
    if (valueIndex >= ready) {
        return {};
    }
    return rb::core::Span<const uint8_t>(bank_.data() + static_cast<std::size_t>(valueIndex) * 2u,
                                         static_cast<std::size_t>(ready - valueIndex) * 2u);
}

StartGate FskLoader::startGate() const noexcept
{
    // State first: once Final is seen, the published count is final.
    const LoaderState now = state();
    const bool final = now == LoaderState::Final;
    const uint32_t ready = published();
    uint64_t runway = 0;
    for (uint32_t i = 0; i < ready && runway < runwayUs_; ++i) {
        runway += static_cast<uint64_t>(le16(bank_.data() + static_cast<std::size_t>(i) * 2u)) * 100u;
    }
    if ((runway >= runwayUs_ || final) && (ready >= prefillValues_ || final)) {
        return StartGate::Ready;
    }
    const bool dead = now == LoaderState::Failed || now == LoaderState::Stopped;
    return dead ? StartGate::Failed : StartGate::Waiting;
}

void FskLoader::setState(LoaderState state) noexcept
{
    state_.store(static_cast<uint8_t>(state), std::memory_order_release);
}

}  // namespace rb::cassette
