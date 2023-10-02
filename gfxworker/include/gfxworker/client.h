/**
 * (c) 2013 by Mega Limited, Auckland, New Zealand
 *
 * This file is part of the MEGA SDK - Client Access Engine.
 *
 * Applications using the MEGA API must present a valid application key
 * and comply with the the rules set forth in the Terms of Service.
 *
 * The MEGA SDK is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 *
 * @copyright Simplified (2-clause) BSD License.
 *
 * You should have received a copy of the license along with this
 * program.
 */

#pragma once

#include <memory>
#include <assert.h>
#include "mega/gfx/worker/comms.h"

namespace mega {
namespace gfx {

/**
 * @brief The GfxClient class.
 */
class GfxClient
{
private:
    std::unique_ptr<IGfxCommunicationsClient> mComms;
public:

    /**
     * @brief GfxClient cotr
     * @param comms. The implementation of GfxCommunications to be used. GfxClient takes the
     * ownership of the provided object.
     *
     */
    GfxClient(std::unique_ptr<IGfxCommunicationsClient> comms) : mComms{std::move(comms)}
    {
        assert(mComms);
    }

    bool runGfxTask(const std::string& localpath);

    bool runShutDown();

    virtual ~GfxClient() = default;
};


} //namespace gfx
} //namespace mega

