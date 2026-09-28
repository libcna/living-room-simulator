// SPDX-License-Identifier: MS-PL
#pragma once


namespace CnaRoom::Effects {

/** @addtogroup cnaext_engine
 *  @{
 */

    /** @brief Overall render quality preset. */
    enum class RenderQuality
    {
        /** @brief Minimum quality — maximises frame rate on low-end hardware. */
        Low,
        /** @brief Balanced quality and performance. */
        Medium,
        /** @brief High quality with minor performance cost. */
        High,
        /** @brief Maximum quality regardless of performance cost. */
        Ultra,
    };

/** @} */ // end of cnaext_engine

} // namespace CnaRoom::Effects
