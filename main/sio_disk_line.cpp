#include "sio_disk_line.hpp"

namespace rb::menusio {

bool UartDiskLine::park() noexcept { return line_ != nullptr && line_->park(); }

rb::host::LineResult UartDiskLine::resume() noexcept
{
    return line_ != nullptr && line_->resume() ? rb::host::LineResult::Done : rb::host::LineResult::Failed;
}

}  // namespace rb::menusio
