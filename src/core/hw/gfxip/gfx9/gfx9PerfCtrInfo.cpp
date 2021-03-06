/*
 ***********************************************************************************************************************
 *
 *  Copyright (c) 2016-2019 Advanced Micro Devices, Inc. All Rights Reserved.
 *
 *  Permission is hereby granted, free of charge, to any person obtaining a copy
 *  of this software and associated documentation files (the "Software"), to deal
 *  in the Software without restriction, including without limitation the rights
 *  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 *  copies of the Software, and to permit persons to whom the Software is
 *  furnished to do so, subject to the following conditions:
 *
 *  The above copyright notice and this permission notice shall be included in all
 *  copies or substantial portions of the Software.
 *
 *  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 *  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 *  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 *  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 *  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 *  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 *  SOFTWARE.
 *
 **********************************************************************************************************************/

#include "core/device.h"
#include "core/hw/gfxip/gfx9/gfx9Chip.h"
#include "core/hw/gfxip/gfx9/gfx9CmdUtil.h"
#include "core/hw/gfxip/gfx9/gfx9PerfCtrInfo.h"

using namespace Util;

namespace Pal
{
namespace Gfx9
{

// These enums are defined by the SPM spec. They map block names to RLC-specific SPM block select codes.
enum Gfx9SpmGlobalBlockSelect : uint32
{
    Gfx9SpmGlobalBlockSelectCpg = 0x0,
    Gfx9SpmGlobalBlockSelectCpc = 0x1,
    Gfx9SpmGlobalBlockSelectCpf = 0x2,
    Gfx9SpmGlobalBlockSelectGds = 0x3,
    Gfx9SpmGlobalBlockSelectTcc = 0x4,
    Gfx9SpmGlobalBlockSelectTca = 0x5,
    Gfx9SpmGlobalBlockSelectIa  = 0x6
};

enum Gfx9SpmSeBlockSelect : uint32
{
     Gfx9SpmSeBlockSelectCb  = 0x0,
     Gfx9SpmSeBlockSelectDb  = 0x1,
     Gfx9SpmSeBlockSelectPa  = 0x2,
     Gfx9SpmSeBlockSelectSx  = 0x3,
     Gfx9SpmSeBlockSelectSc  = 0x4,
     Gfx9SpmSeBlockSelectTa  = 0x5,
     Gfx9SpmSeBlockSelectTd  = 0x6,
     Gfx9SpmSeBlockSelectTcp = 0x7,
     Gfx9SpmSeBlockSelectSpi = 0x8,
     Gfx9SpmSeBlockSelectSqg = 0x9,
     Gfx9SpmSeBlockSelectVgt = 0xA,
     Gfx9SpmSeBlockSelectRmi = 0xB
};

// =====================================================================================================================
// A helper function which finds the proper SC max event ID.
static uint32 GetScMaxEventId(
    const Pal::Device& device)
{
    uint32 maxEventId = 0;

    if (IsGfx9(device))
    {
        if (IsVega12(device))
        {
            maxEventId = MaxScPerfcntSelVg12;
        }
        else if (IsVega20(device))
        {
            maxEventId = MaxScPerfcntSelVg20;
        }
        else if (IsRaven2(device))
        {
            maxEventId = MaxScPerfcntSelRv2x;
        }
        else
        {
            maxEventId = MaxScPerfcntSelGfx09_0;
        }
    }

    return maxEventId;
}

// There's a terrifyingly large number of UMCCH registers. This macro makes UpdateUmcchBlockInfo much more sane.
#define SET_UMCCH_INSTANCE_REGS(Ns, Idx) \
    pInfo->umcchRegAddr[Idx].perfMonCtlClk = Ns::mmUMCCH##Idx##_PerfMonCtlClk; \
    pInfo->umcchRegAddr[Idx].perModule[0] = { Ns::mmUMCCH##Idx##_PerfMonCtl1, Ns::mmUMCCH##Idx##_PerfMonCtr1_Lo, Ns::mmUMCCH##Idx##_PerfMonCtr1_Hi }; \
    pInfo->umcchRegAddr[Idx].perModule[1] = { Ns::mmUMCCH##Idx##_PerfMonCtl2, Ns::mmUMCCH##Idx##_PerfMonCtr2_Lo, Ns::mmUMCCH##Idx##_PerfMonCtr2_Hi }; \
    pInfo->umcchRegAddr[Idx].perModule[2] = { Ns::mmUMCCH##Idx##_PerfMonCtl3, Ns::mmUMCCH##Idx##_PerfMonCtr3_Lo, Ns::mmUMCCH##Idx##_PerfMonCtr3_Hi }; \
    pInfo->umcchRegAddr[Idx].perModule[3] = { Ns::mmUMCCH##Idx##_PerfMonCtl4, Ns::mmUMCCH##Idx##_PerfMonCtr4_Lo, Ns::mmUMCCH##Idx##_PerfMonCtr4_Hi }; \
    pInfo->umcchRegAddr[Idx].perModule[4] = { Ns::mmUMCCH##Idx##_PerfMonCtl5, Ns::mmUMCCH##Idx##_PerfMonCtr5_Lo, Ns::mmUMCCH##Idx##_PerfMonCtr5_Hi };

// =====================================================================================================================
// A helper function which updates the UMCCH's block info with device-specific data.
static void UpdateUmcchBlockInfo(
    const Pal::Device&    device,
    Gfx9PerfCounterInfo*  pInfo,
    PerfCounterBlockInfo* pBlockInfo)
{
    // The first instance's registers are common to all ASICs, the rest are a total mess.
    pInfo->umcchRegAddr[0].perfMonCtlClk = mmUMCCH0_PerfMonCtlClk;
    pInfo->umcchRegAddr[0].perModule[0] = { mmUMCCH0_PerfMonCtl1, mmUMCCH0_PerfMonCtr1_Lo, mmUMCCH0_PerfMonCtr1_Hi };
    pInfo->umcchRegAddr[0].perModule[1] = { mmUMCCH0_PerfMonCtl2, mmUMCCH0_PerfMonCtr2_Lo, mmUMCCH0_PerfMonCtr2_Hi };
    pInfo->umcchRegAddr[0].perModule[2] = { mmUMCCH0_PerfMonCtl3, mmUMCCH0_PerfMonCtr3_Lo, mmUMCCH0_PerfMonCtr3_Hi };
    pInfo->umcchRegAddr[0].perModule[3] = { mmUMCCH0_PerfMonCtl4, mmUMCCH0_PerfMonCtr4_Lo, mmUMCCH0_PerfMonCtr4_Hi };
    pInfo->umcchRegAddr[0].perModule[4] = { mmUMCCH0_PerfMonCtl5, mmUMCCH0_PerfMonCtr5_Lo, mmUMCCH0_PerfMonCtr5_Hi };

    if (IsGfx9(device))
    {
        if (device.ChipProperties().familyId == FAMILY_AI)
        {
            SET_UMCCH_INSTANCE_REGS(Vega, 1);
            SET_UMCCH_INSTANCE_REGS(Vega, 2);
            SET_UMCCH_INSTANCE_REGS(Vega, 3);
            SET_UMCCH_INSTANCE_REGS(Vega, 4);
            SET_UMCCH_INSTANCE_REGS(Vega, 5);
            SET_UMCCH_INSTANCE_REGS(Vega, 6);
            SET_UMCCH_INSTANCE_REGS(Vega, 7);

            if (IsVega10(device))
            {
                SET_UMCCH_INSTANCE_REGS(Vg10, 8);
                SET_UMCCH_INSTANCE_REGS(Vg10, 9);
                SET_UMCCH_INSTANCE_REGS(Vg10, 10);
                SET_UMCCH_INSTANCE_REGS(Vg10, 11);
                SET_UMCCH_INSTANCE_REGS(Vg10, 12);
                SET_UMCCH_INSTANCE_REGS(Vg10, 13);
                SET_UMCCH_INSTANCE_REGS(Vg10, 14);
                SET_UMCCH_INSTANCE_REGS(Vg10, 15);
            }
            else if (IsVega20(device))
            {
                SET_UMCCH_INSTANCE_REGS(Vg20, 8);
                SET_UMCCH_INSTANCE_REGS(Vg20, 9);
                SET_UMCCH_INSTANCE_REGS(Vg20, 10);
                SET_UMCCH_INSTANCE_REGS(Vg20, 11);
                SET_UMCCH_INSTANCE_REGS(Vg20, 12);
                SET_UMCCH_INSTANCE_REGS(Vg20, 13);
                SET_UMCCH_INSTANCE_REGS(Vg20, 14);
                SET_UMCCH_INSTANCE_REGS(Vg20, 15);
                SET_UMCCH_INSTANCE_REGS(Vg20, 16);
                SET_UMCCH_INSTANCE_REGS(Vg20, 17);
                SET_UMCCH_INSTANCE_REGS(Vg20, 18);
                SET_UMCCH_INSTANCE_REGS(Vg20, 19);
                SET_UMCCH_INSTANCE_REGS(Vg20, 20);
                SET_UMCCH_INSTANCE_REGS(Vg20, 21);
                SET_UMCCH_INSTANCE_REGS(Vg20, 22);
                SET_UMCCH_INSTANCE_REGS(Vg20, 23);
                SET_UMCCH_INSTANCE_REGS(Vg20, 24);
                SET_UMCCH_INSTANCE_REGS(Vg20, 25);
                SET_UMCCH_INSTANCE_REGS(Vg20, 26);
                SET_UMCCH_INSTANCE_REGS(Vg20, 27);
                SET_UMCCH_INSTANCE_REGS(Vg20, 28);
                SET_UMCCH_INSTANCE_REGS(Vg20, 29);
                SET_UMCCH_INSTANCE_REGS(Vg20, 30);
                SET_UMCCH_INSTANCE_REGS(Vg20, 31);
            }
        }
        else
        {
            SET_UMCCH_INSTANCE_REGS(Raven, 1);
        }
    }

    // We should have one UMC channel per SDP interface. We also should have a full set of registers for each of those
    // channels. However, we might not be able to read or write some of them due to a limitation in the CP's COPY_DATA
    // packet. We shouldn't expose any instances that could hit that limitation.
    pBlockInfo->numInstances = 0;

    for (uint32 instance = 0; instance < device.ChipProperties().gfx9.numSdpInterfaces; ++instance)
    {
        bool regsAreValid = CmdUtil::CanUseCopyDataRegOffset(pInfo->umcchRegAddr[instance].perfMonCtlClk);

        for (uint32 idx = 0; regsAreValid && (idx < Gfx9MaxUmcchPerfModules); ++idx)
        {
            regsAreValid =
                (CmdUtil::CanUseCopyDataRegOffset(pInfo->umcchRegAddr[instance].perModule[idx].perfMonCtl)   &&
                 CmdUtil::CanUseCopyDataRegOffset(pInfo->umcchRegAddr[instance].perModule[idx].perfMonCtrLo) &&
                 CmdUtil::CanUseCopyDataRegOffset(pInfo->umcchRegAddr[instance].perModule[idx].perfMonCtrHi));
        }

        if (regsAreValid == false)
        {
            // Drop out now, don't expose this instance or any after it.
            break;
        }
        else
        {
            // This instance is good, check the next one.
            pBlockInfo->numInstances++;
        }
    }
}

// =====================================================================================================================
// A helper function which finds the proper CB max event ID.
static uint32 Gfx9GetCbMaxEventId(
    const Pal::Device& device)
{
    uint32 maxEventId = 0;

    {
        static_assert(MaxCBPerfSelVega == MaxCBPerfSelRv1x, "Max CB perf counter ID doesn't match!");
        static_assert(MaxCBPerfSelVega == MaxCBPerfSelRv2x, "Max CB perf counter ID doesn't match!");

        maxEventId = MaxCBPerfSelVega;
    }

    return maxEventId;
}

// =====================================================================================================================
// A helper function which finds the proper CPG max event ID.
static uint32 Gfx9GetCpgMaxEventId(
    const Pal::Device& device)
{
    uint32 maxEventId = 0;

    if (IsRaven(device))
    {
        maxEventId = MaxCpgPerfcountSelRv1x;
    }
    else if (IsRaven2(device))
    {
        maxEventId = MaxCpgPerfcountSelRv2x;
    }
    else
    {
        maxEventId = MaxCpgPerfcountSelVega;
    }

    return maxEventId;
}

// =====================================================================================================================
// A helper function which updates the RPB's block info with device-specific data.
static void Gfx9UpdateRpbBlockInfo(
    const Pal::Device&    device,
    PerfCounterBlockInfo* pInfo)
{
    {
        static_assert(Rv1x::mmRPB_PERFCOUNTER0_CFG == Vega::mmRPB_PERFCOUNTER0_CFG, "Must fix RPB registers!");
        static_assert(Rv2x::mmRPB_PERFCOUNTER0_CFG == Vega::mmRPB_PERFCOUNTER0_CFG, "Must fix RPB registers!");

        pInfo->regAddr = { Vega::mmRPB_PERFCOUNTER_RSLT_CNTL, {
            { Vega::mmRPB_PERFCOUNTER0_CFG, 0, Vega::mmRPB_PERFCOUNTER_LO, Vega::mmRPB_PERFCOUNTER_HI },
            { Vega::mmRPB_PERFCOUNTER1_CFG, 0, Vega::mmRPB_PERFCOUNTER_LO, Vega::mmRPB_PERFCOUNTER_HI },
            { Vega::mmRPB_PERFCOUNTER2_CFG, 0, Vega::mmRPB_PERFCOUNTER_LO, Vega::mmRPB_PERFCOUNTER_HI },
            { Vega::mmRPB_PERFCOUNTER3_CFG, 0, Vega::mmRPB_PERFCOUNTER_LO, Vega::mmRPB_PERFCOUNTER_HI },
        }};
    }
}

// =====================================================================================================================
// Initializes each block's basic hardware-defined information (distribution, numInstances, numGenericSpmModules, etc.)
static void Gfx9InitBasicBlockInfo(
    const Pal::Device& device,
    GpuChipProperties* pProps)
{
    Gfx9PerfCounterInfo*const pInfo    = &pProps->gfx9.perfCounterInfo;
    const bool                isGfx9_0 = (IsVega10(device) || IsRaven(device));

    // Pull in the generic gfx9 registers to make it easier to read the register tables.
    using namespace Gfx09;

    // Start by hard-coding hardware specific constants for each block. The shared blocks come first followed by
    // gfxip-specific blocks. Note that these blocks doesn't exist on any gfx9+ ASICs: SRBM, MC, TCS.
    //
    // The distribution and numInstances (per-distribution) are derived from our hardware architecture.
    // The generic module counts are determined by:
    //   1. Does the block follow the generic programming model as defined by the perf experiment code?
    //   2. If so, there's one SPM module for each SELECT/SELECT1 pair and one legacy module for the remaining SELECTs.
    // The number of SPM wires is a hardware constant baked into each ASIC's design. So are the SPM block selects.
    // The maximum event IDs are the largest values from the hardware perf_sel enums.
    // Finally, we hard-code the PERFCOUNTER# register addresses for each module.

    PerfCounterBlockInfo*const pCpf = &pInfo->block[static_cast<uint32>(GpuBlock::Cpf)];
    pCpf->distribution              = PerfCounterDistribution::GlobalBlock;
    pCpf->numInstances              = 1;
    pCpf->numGenericSpmModules      = 1; // CPF_PERFCOUNTER0
    pCpf->numGenericLegacyModules   = 1; // CPF_PERFCOUNTER1
    pCpf->numSpmWires               = 2;
    pCpf->spmBlockSelect            = Gfx9SpmGlobalBlockSelectCpf;
    pCpf->maxEventId                = MaxCpfPerfcountSelGfx09;

    pCpf->regAddr = { 0, {
        { mmCPF_PERFCOUNTER0_SELECT, mmCPF_PERFCOUNTER0_SELECT1, mmCPF_PERFCOUNTER0_LO, mmCPF_PERFCOUNTER0_HI },
        { mmCPF_PERFCOUNTER1_SELECT, 0,                          mmCPF_PERFCOUNTER1_LO, mmCPF_PERFCOUNTER1_HI },
    }};

    PerfCounterBlockInfo*const pIa = &pInfo->block[static_cast<uint32>(GpuBlock::Ia)];
    pIa->distribution              = PerfCounterDistribution::GlobalBlock;
    pIa->numInstances              = Max(pProps->gfx9.numShaderEngines / 2u, 1u);
    pIa->numGenericSpmModules      = 1; // IA_PERFCOUNTER0
    pIa->numGenericLegacyModules   = 3; // IA_PERFCOUNTER1-3
    pIa->numSpmWires               = 2;
    pIa->spmBlockSelect            = Gfx9SpmGlobalBlockSelectIa;
    pIa->maxEventId                = isGfx9_0 ? MaxIaPerfcountSelectGfx09_0 : MaxIaPerfcountSelectGfx09_1x;

    pIa->regAddr = { 0, {
        { mmIA_PERFCOUNTER0_SELECT, mmIA_PERFCOUNTER0_SELECT1, mmIA_PERFCOUNTER0_LO, mmIA_PERFCOUNTER0_HI },
        { mmIA_PERFCOUNTER1_SELECT, 0,                         mmIA_PERFCOUNTER1_LO, mmIA_PERFCOUNTER1_HI },
        { mmIA_PERFCOUNTER2_SELECT, 0,                         mmIA_PERFCOUNTER2_LO, mmIA_PERFCOUNTER2_HI },
        { mmIA_PERFCOUNTER3_SELECT, 0,                         mmIA_PERFCOUNTER3_LO, mmIA_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pVgt = &pInfo->block[static_cast<uint32>(GpuBlock::Vgt)];
    pVgt->distribution              = PerfCounterDistribution::PerShaderEngine;
    pVgt->numInstances              = 1;
    pVgt->numGenericSpmModules      = 2; // VGT_PERFCOUNTER0-1
    pVgt->numGenericLegacyModules   = 2; // VGT_PERFCOUNTER2-3
    pVgt->numSpmWires               = 3;
    pVgt->spmBlockSelect            = Gfx9SpmSeBlockSelectVgt;
    pVgt->maxEventId                = MaxVgtPerfcountSelect;

    pVgt->regAddr = { 0, {
        { mmVGT_PERFCOUNTER0_SELECT, mmVGT_PERFCOUNTER0_SELECT1, mmVGT_PERFCOUNTER0_LO, mmVGT_PERFCOUNTER0_HI },
        { mmVGT_PERFCOUNTER1_SELECT, mmVGT_PERFCOUNTER1_SELECT1, mmVGT_PERFCOUNTER1_LO, mmVGT_PERFCOUNTER1_HI },
        { mmVGT_PERFCOUNTER2_SELECT, 0,                          mmVGT_PERFCOUNTER2_LO, mmVGT_PERFCOUNTER2_HI },
        { mmVGT_PERFCOUNTER3_SELECT, 0,                          mmVGT_PERFCOUNTER3_LO, mmVGT_PERFCOUNTER3_HI },
    }};

    // Note that the PA uses the SU select enum.
    PerfCounterBlockInfo*const pPa = &pInfo->block[static_cast<uint32>(GpuBlock::Pa)];
    pPa->distribution              = PerfCounterDistribution::PerShaderEngine;
    pPa->numInstances              = 1;
    pPa->numGenericSpmModules      = 2; // PA_SU_PERFCOUNTER0-1
    pPa->numGenericLegacyModules   = 2; // PA_SU_PERFCOUNTER2-3
    pPa->numSpmWires               = 3;
    pPa->spmBlockSelect            = Gfx9SpmSeBlockSelectPa;
    pPa->maxEventId                = isGfx9_0 ? MaxSuPerfcntSelGfx09_0 : MaxSuPerfcntSelGfx09_1x;

    pPa->regAddr = { 0, {
        { mmPA_SU_PERFCOUNTER0_SELECT, mmPA_SU_PERFCOUNTER0_SELECT1, mmPA_SU_PERFCOUNTER0_LO, mmPA_SU_PERFCOUNTER0_HI },
        { mmPA_SU_PERFCOUNTER1_SELECT, mmPA_SU_PERFCOUNTER1_SELECT1, mmPA_SU_PERFCOUNTER1_LO, mmPA_SU_PERFCOUNTER1_HI },
        { mmPA_SU_PERFCOUNTER2_SELECT, 0,                            mmPA_SU_PERFCOUNTER2_LO, mmPA_SU_PERFCOUNTER2_HI },
        { mmPA_SU_PERFCOUNTER3_SELECT, 0,                            mmPA_SU_PERFCOUNTER3_LO, mmPA_SU_PERFCOUNTER3_HI },
    }};

    // Note that between gfx6 and now the SC switched to per-shader-array.
    PerfCounterBlockInfo*const pSc = &pInfo->block[static_cast<uint32>(GpuBlock::Sc)];
    pSc->distribution              = PerfCounterDistribution::PerShaderArray;
    pSc->numInstances              = 1;
    pSc->numGenericSpmModules      = 1; // PA_SC_PERFCOUNTER0
    pSc->numGenericLegacyModules   = 7; // PA_SC_PERFCOUNTER1-7
    pSc->numSpmWires               = 2;
    pSc->spmBlockSelect            = Gfx9SpmSeBlockSelectSc;
    pSc->maxEventId                = GetScMaxEventId(device);

    pSc->regAddr = { 0, {
        { mmPA_SC_PERFCOUNTER0_SELECT, mmPA_SC_PERFCOUNTER0_SELECT1, mmPA_SC_PERFCOUNTER0_LO, mmPA_SC_PERFCOUNTER0_HI },
        { mmPA_SC_PERFCOUNTER1_SELECT, 0,                            mmPA_SC_PERFCOUNTER1_LO, mmPA_SC_PERFCOUNTER1_HI },
        { mmPA_SC_PERFCOUNTER2_SELECT, 0,                            mmPA_SC_PERFCOUNTER2_LO, mmPA_SC_PERFCOUNTER2_HI },
        { mmPA_SC_PERFCOUNTER3_SELECT, 0,                            mmPA_SC_PERFCOUNTER3_LO, mmPA_SC_PERFCOUNTER3_HI },
        { mmPA_SC_PERFCOUNTER4_SELECT, 0,                            mmPA_SC_PERFCOUNTER4_LO, mmPA_SC_PERFCOUNTER4_HI },
        { mmPA_SC_PERFCOUNTER5_SELECT, 0,                            mmPA_SC_PERFCOUNTER5_LO, mmPA_SC_PERFCOUNTER5_HI },
        { mmPA_SC_PERFCOUNTER6_SELECT, 0,                            mmPA_SC_PERFCOUNTER6_LO, mmPA_SC_PERFCOUNTER6_HI },
        { mmPA_SC_PERFCOUNTER7_SELECT, 0,                            mmPA_SC_PERFCOUNTER7_LO, mmPA_SC_PERFCOUNTER7_HI },
    }};

    PerfCounterBlockInfo*const pSpi = &pInfo->block[static_cast<uint32>(GpuBlock::Spi)];
    pSpi->distribution              = PerfCounterDistribution::PerShaderEngine;
    pSpi->numInstances              = 1;
    pSpi->numGenericSpmModules      = 4; // SPI_PERFCOUNTER0-3
    pSpi->numGenericLegacyModules   = 2; // SPI_PERFCOUNTER4-5
    pSpi->numSpmWires               = 8;
    pSpi->spmBlockSelect            = Gfx9SpmSeBlockSelectSpi;
    pSpi->maxEventId                = MaxSpiPerfcntSelGfx09;

    pSpi->regAddr = { 0, {
        { mmSPI_PERFCOUNTER0_SELECT, mmSPI_PERFCOUNTER0_SELECT1, mmSPI_PERFCOUNTER0_LO, mmSPI_PERFCOUNTER0_HI },
        { mmSPI_PERFCOUNTER1_SELECT, mmSPI_PERFCOUNTER1_SELECT1, mmSPI_PERFCOUNTER1_LO, mmSPI_PERFCOUNTER1_HI },
        { mmSPI_PERFCOUNTER2_SELECT, mmSPI_PERFCOUNTER2_SELECT1, mmSPI_PERFCOUNTER2_LO, mmSPI_PERFCOUNTER2_HI },
        { mmSPI_PERFCOUNTER3_SELECT, mmSPI_PERFCOUNTER3_SELECT1, mmSPI_PERFCOUNTER3_LO, mmSPI_PERFCOUNTER3_HI },
        { mmSPI_PERFCOUNTER4_SELECT, 0,                          mmSPI_PERFCOUNTER4_LO, mmSPI_PERFCOUNTER4_HI },
        { mmSPI_PERFCOUNTER5_SELECT, 0,                          mmSPI_PERFCOUNTER5_LO, mmSPI_PERFCOUNTER5_HI },
    }};

    // The SQ counters are implemented by a single SQG in every shader engine. It has a unique programming model.
    // The SQ counter modules can be a global counter or one 32-bit SPM counter. 16-bit SPM is not supported but we
    // fake one 16-bit counter for now. All gfx9 ASICs only contain 8 out of the possible 16 counter modules.
    PerfCounterBlockInfo*const pSq = &pInfo->block[static_cast<uint32>(GpuBlock::Sq)];
    pSq->distribution              = PerfCounterDistribution::PerShaderEngine;
    pSq->numInstances              = 1;
    pSq->num16BitSpmCounters       = 8;
    pSq->num32BitSpmCounters       = 8;
    pSq->numGlobalSharedCounters   = 8;
    pSq->numGenericSpmModules      = 0;
    pSq->numGenericLegacyModules   = 0;
    pSq->numSpmWires               = 8;
    pSq->spmBlockSelect            = Gfx9SpmSeBlockSelectSqg;
    pSq->maxEventId                = MaxSqPerfSelGfx09;

    pSq->regAddr = { 0, {
        { mmSQ_PERFCOUNTER0_SELECT, 0, mmSQ_PERFCOUNTER0_LO, mmSQ_PERFCOUNTER0_HI },
        { mmSQ_PERFCOUNTER1_SELECT, 0, mmSQ_PERFCOUNTER1_LO, mmSQ_PERFCOUNTER1_HI },
        { mmSQ_PERFCOUNTER2_SELECT, 0, mmSQ_PERFCOUNTER2_LO, mmSQ_PERFCOUNTER2_HI },
        { mmSQ_PERFCOUNTER3_SELECT, 0, mmSQ_PERFCOUNTER3_LO, mmSQ_PERFCOUNTER3_HI },
        { mmSQ_PERFCOUNTER4_SELECT, 0, mmSQ_PERFCOUNTER4_LO, mmSQ_PERFCOUNTER4_HI },
        { mmSQ_PERFCOUNTER5_SELECT, 0, mmSQ_PERFCOUNTER5_LO, mmSQ_PERFCOUNTER5_HI },
        { mmSQ_PERFCOUNTER6_SELECT, 0, mmSQ_PERFCOUNTER6_LO, mmSQ_PERFCOUNTER6_HI },
        { mmSQ_PERFCOUNTER7_SELECT, 0, mmSQ_PERFCOUNTER7_LO, mmSQ_PERFCOUNTER7_HI },
    }};

    // Note that between gfx6 and now the SX switched to per-shader-engine.
    PerfCounterBlockInfo*const pSx = &pInfo->block[static_cast<uint32>(GpuBlock::Sx)];
    pSx->distribution              = PerfCounterDistribution::PerShaderEngine;
    pSx->numInstances              = 1;
    pSx->numGenericSpmModules      = 2; // SX_PERFCOUNTER0-1
    pSx->numGenericLegacyModules   = 2; // SX_PERFCOUNTER2-3
    pSx->numSpmWires               = 4;
    pSx->spmBlockSelect            = Gfx9SpmSeBlockSelectSx;
    pSx->maxEventId                = MaxSxPerfcounterValsGfx09;

    pSx->regAddr = { 0, {
        { mmSX_PERFCOUNTER0_SELECT, mmSX_PERFCOUNTER0_SELECT1, mmSX_PERFCOUNTER0_LO, mmSX_PERFCOUNTER0_HI },
        { mmSX_PERFCOUNTER1_SELECT, mmSX_PERFCOUNTER1_SELECT1, mmSX_PERFCOUNTER1_LO, mmSX_PERFCOUNTER1_HI },
        { mmSX_PERFCOUNTER2_SELECT, 0,                         mmSX_PERFCOUNTER2_LO, mmSX_PERFCOUNTER2_HI },
        { mmSX_PERFCOUNTER3_SELECT, 0,                         mmSX_PERFCOUNTER3_LO, mmSX_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pTa = &pInfo->block[static_cast<uint32>(GpuBlock::Ta)];
    pTa->distribution              = PerfCounterDistribution::PerShaderArray;
    pTa->numInstances              = pProps->gfx9.numCuPerSh;
    pTa->numGenericSpmModules      = 1; // TA_PERFCOUNTER0
    pTa->numGenericLegacyModules   = 1; // TA_PERFCOUNTER1
    pTa->numSpmWires               = 2;
    pTa->spmBlockSelect            = Gfx9SpmSeBlockSelectTa;
    pTa->maxEventId                = MaxTaPerfcountSelGfx09;

    pTa->regAddr = { 0, {
        { mmTA_PERFCOUNTER0_SELECT, mmTA_PERFCOUNTER0_SELECT1, mmTA_PERFCOUNTER0_LO, mmTA_PERFCOUNTER0_HI },
        { mmTA_PERFCOUNTER1_SELECT, 0,                         mmTA_PERFCOUNTER1_LO, mmTA_PERFCOUNTER1_HI },
    }};

    PerfCounterBlockInfo*const pTd = &pInfo->block[static_cast<uint32>(GpuBlock::Td)];
    pTd->distribution              = PerfCounterDistribution::PerShaderArray;
    pTd->numInstances              = pProps->gfx9.numCuPerSh;
    pTd->numGenericSpmModules      = 1; // TD_PERFCOUNTER0
    pTd->numGenericLegacyModules   = 1; // TD_PERFCOUNTER1
    pTd->numSpmWires               = 2;
    pTd->spmBlockSelect            = Gfx9SpmSeBlockSelectTd;
    pTd->maxEventId                = MaxTdPerfcountSelGfx09;

    pTd->regAddr = { 0, {
        { mmTD_PERFCOUNTER0_SELECT, mmTD_PERFCOUNTER0_SELECT1, mmTD_PERFCOUNTER0_LO, mmTD_PERFCOUNTER0_HI },
        { mmTD_PERFCOUNTER1_SELECT, 0,                         mmTD_PERFCOUNTER1_LO, mmTD_PERFCOUNTER1_HI },
    }};

    PerfCounterBlockInfo*const pTcp = &pInfo->block[static_cast<uint32>(GpuBlock::Tcp)];
    pTcp->distribution              = PerfCounterDistribution::PerShaderArray;
    pTcp->numInstances              = pProps->gfx9.numCuPerSh;
    pTcp->numGenericSpmModules      = 2; // TCP_PERFCOUNTER0-1
    pTcp->numGenericLegacyModules   = 2; // TCP_PERFCOUNTER2-3
    pTcp->numSpmWires               = 3;
    pTcp->spmBlockSelect            = Gfx9SpmSeBlockSelectTcp;
    pTcp->maxEventId                = MaxTcpPerfcountSelectGfx09;

    pTcp->regAddr = { 0, {
        { mmTCP_PERFCOUNTER0_SELECT, mmTCP_PERFCOUNTER0_SELECT1, mmTCP_PERFCOUNTER0_LO, mmTCP_PERFCOUNTER0_HI },
        { mmTCP_PERFCOUNTER1_SELECT, mmTCP_PERFCOUNTER1_SELECT1, mmTCP_PERFCOUNTER1_LO, mmTCP_PERFCOUNTER1_HI },
        { mmTCP_PERFCOUNTER2_SELECT, 0,                          mmTCP_PERFCOUNTER2_LO, mmTCP_PERFCOUNTER2_HI },
        { mmTCP_PERFCOUNTER3_SELECT, 0,                          mmTCP_PERFCOUNTER3_LO, mmTCP_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pTcc = &pInfo->block[static_cast<uint32>(GpuBlock::Tcc)];
    pTcc->distribution              = PerfCounterDistribution::GlobalBlock,
    pTcc->numInstances              = pProps->gfx9.numTccBlocks;
    pTcc->numGenericSpmModules      = 2; // TCC_PERFCOUNTER0-1
    pTcc->numGenericLegacyModules   = 2; // TCC_PERFCOUNTER2-3
    pTcc->numSpmWires               = 4;
    pTcc->spmBlockSelect            = Gfx9SpmGlobalBlockSelectTcc;
    pTcc->maxEventId                = MaxTccPerfSelVg10_Vg12;

    static_assert(MaxTccPerfSelVg10_Vg12 == MaxTccPerfSelRaven, "Max TCC perf counter ID doesn't match!");

    pTcc->regAddr = { 0, {
        { mmTCC_PERFCOUNTER0_SELECT, mmTCC_PERFCOUNTER0_SELECT1, mmTCC_PERFCOUNTER0_LO, mmTCC_PERFCOUNTER0_HI },
        { mmTCC_PERFCOUNTER1_SELECT, mmTCC_PERFCOUNTER1_SELECT1, mmTCC_PERFCOUNTER1_LO, mmTCC_PERFCOUNTER1_HI },
        { mmTCC_PERFCOUNTER2_SELECT, 0,                          mmTCC_PERFCOUNTER2_LO, mmTCC_PERFCOUNTER2_HI },
        { mmTCC_PERFCOUNTER3_SELECT, 0,                          mmTCC_PERFCOUNTER3_LO, mmTCC_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pTca = &pInfo->block[static_cast<uint32>(GpuBlock::Tca)];
    pTca->distribution              = PerfCounterDistribution::GlobalBlock,
    pTca->numInstances              = 2;
    pTca->numGenericSpmModules      = 2; // TCA_PERFCOUNTER0-1
    pTca->numGenericLegacyModules   = 2; // TCA_PERFCOUNTER2-3
    pTca->numSpmWires               = 4;
    pTca->spmBlockSelect            = Gfx9SpmGlobalBlockSelectTca;
    pTca->maxEventId                = MaxTcaPerfSel;

    pTca->regAddr = { 0, {
        { mmTCA_PERFCOUNTER0_SELECT, mmTCA_PERFCOUNTER0_SELECT1, mmTCA_PERFCOUNTER0_LO, mmTCA_PERFCOUNTER0_HI },
        { mmTCA_PERFCOUNTER1_SELECT, mmTCA_PERFCOUNTER1_SELECT1, mmTCA_PERFCOUNTER1_LO, mmTCA_PERFCOUNTER1_HI },
        { mmTCA_PERFCOUNTER2_SELECT, 0,                          mmTCA_PERFCOUNTER2_LO, mmTCA_PERFCOUNTER2_HI },
        { mmTCA_PERFCOUNTER3_SELECT, 0,                          mmTCA_PERFCOUNTER3_LO, mmTCA_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pDb = &pInfo->block[static_cast<uint32>(GpuBlock::Db)];
    pDb->distribution              = PerfCounterDistribution::PerShaderArray;
    pDb->numInstances              = pProps->gfx9.maxNumRbPerSe / pProps->gfx9.numShaderArrays;
    pDb->numGenericSpmModules      = 2; // DB_PERFCOUNTER0-1
    pDb->numGenericLegacyModules   = 2; // DB_PERFCOUNTER2-3
    pDb->numSpmWires               = 3;
    pDb->spmBlockSelect            = Gfx9SpmSeBlockSelectDb;
    pDb->maxEventId                = MaxPerfcounterValsGfx09;

    pDb->regAddr = { 0, {
        { mmDB_PERFCOUNTER0_SELECT, mmDB_PERFCOUNTER0_SELECT1, mmDB_PERFCOUNTER0_LO, mmDB_PERFCOUNTER0_HI },
        { mmDB_PERFCOUNTER1_SELECT, mmDB_PERFCOUNTER1_SELECT1, mmDB_PERFCOUNTER1_LO, mmDB_PERFCOUNTER1_HI },
        { mmDB_PERFCOUNTER2_SELECT, 0,                         mmDB_PERFCOUNTER2_LO, mmDB_PERFCOUNTER2_HI },
        { mmDB_PERFCOUNTER3_SELECT, 0,                         mmDB_PERFCOUNTER3_LO, mmDB_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pCb = &pInfo->block[static_cast<uint32>(GpuBlock::Cb)];
    pCb->distribution              = PerfCounterDistribution::PerShaderArray;
    pCb->numInstances              = pProps->gfx9.maxNumRbPerSe / pProps->gfx9.numShaderArrays;
    pCb->numGenericSpmModules      = 1; // CB_PERFCOUNTER0
    pCb->numGenericLegacyModules   = 3; // CB_PERFCOUNTER1-3
    pCb->numSpmWires               = 2;
    pCb->spmBlockSelect            = Gfx9SpmSeBlockSelectCb;
    pCb->maxEventId                = Gfx9GetCbMaxEventId(device);

    pCb->regAddr = { 0, {
        { mmCB_PERFCOUNTER0_SELECT, mmCB_PERFCOUNTER0_SELECT1, mmCB_PERFCOUNTER0_LO, mmCB_PERFCOUNTER0_HI },
        { mmCB_PERFCOUNTER1_SELECT, 0,                         mmCB_PERFCOUNTER1_LO, mmCB_PERFCOUNTER1_HI },
        { mmCB_PERFCOUNTER2_SELECT, 0,                         mmCB_PERFCOUNTER2_LO, mmCB_PERFCOUNTER2_HI },
        { mmCB_PERFCOUNTER3_SELECT, 0,                         mmCB_PERFCOUNTER3_LO, mmCB_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pGds = &pInfo->block[static_cast<uint32>(GpuBlock::Gds)];
    pGds->distribution              = PerfCounterDistribution::GlobalBlock;
    pGds->numInstances              = 1;
    pGds->numGenericSpmModules      = 1; // GDS_PERFCOUNTER0
    pGds->numGenericLegacyModules   = 3; // GDS_PERFCOUNTER1-3
    pGds->numSpmWires               = 2;
    pGds->spmBlockSelect            = Gfx9SpmGlobalBlockSelectGds;
    pGds->maxEventId                = MaxGdsPerfcountSelectGfx09;

    pGds->regAddr = { 0, {
        { mmGDS_PERFCOUNTER0_SELECT, mmGDS_PERFCOUNTER0_SELECT1, mmGDS_PERFCOUNTER0_LO, mmGDS_PERFCOUNTER0_HI },
        { mmGDS_PERFCOUNTER1_SELECT, 0,                          mmGDS_PERFCOUNTER1_LO, mmGDS_PERFCOUNTER1_HI },
        { mmGDS_PERFCOUNTER2_SELECT, 0,                          mmGDS_PERFCOUNTER2_LO, mmGDS_PERFCOUNTER2_HI },
        { mmGDS_PERFCOUNTER3_SELECT, 0,                          mmGDS_PERFCOUNTER3_LO, mmGDS_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pGrbm = &pInfo->block[static_cast<uint32>(GpuBlock::Grbm)];
    pGrbm->distribution              = PerfCounterDistribution::GlobalBlock;
    pGrbm->numInstances              = 1;
    pGrbm->numGenericSpmModules      = 0;
    pGrbm->numGenericLegacyModules   = 2; // GRBM_PERFCOUNTER0-1
    pGrbm->numSpmWires               = 0;
    pGrbm->maxEventId                = MaxGrbmPerfSelGfx09;

    pGrbm->regAddr = { 0, {
        { mmGRBM_PERFCOUNTER0_SELECT, 0, mmGRBM_PERFCOUNTER0_LO, mmGRBM_PERFCOUNTER0_HI },
        { mmGRBM_PERFCOUNTER1_SELECT, 0, mmGRBM_PERFCOUNTER1_LO, mmGRBM_PERFCOUNTER1_HI },
    }};

    // These counters are a bit special. The GRBM is a global block but it defines one special counter per SE. We
    // abstract this as a special Grbm(per)Se block which needs special handling in the perf experiment.
    PerfCounterBlockInfo*const pGrbmSe = &pInfo->block[static_cast<uint32>(GpuBlock::GrbmSe)];
    pGrbmSe->distribution              = PerfCounterDistribution::PerShaderEngine;
    pGrbmSe->numInstances              = 1;
    pGrbmSe->numGlobalOnlyCounters     = 1;
    pGrbmSe->numGenericSpmModules      = 0;
    pGrbmSe->numGenericLegacyModules   = 0;
    pGrbmSe->numSpmWires               = 0;
    pGrbmSe->maxEventId                = MaxGrbmSe0PerfSelGfx09;

    // By convention we access the counter register address array using the SE index.
    pGrbmSe->regAddr = { 0, {
        { mmGRBM_SE0_PERFCOUNTER_SELECT, 0, mmGRBM_SE0_PERFCOUNTER_LO, mmGRBM_SE0_PERFCOUNTER_HI },
        { mmGRBM_SE1_PERFCOUNTER_SELECT, 0, mmGRBM_SE1_PERFCOUNTER_LO, mmGRBM_SE1_PERFCOUNTER_HI },
        { mmGRBM_SE2_PERFCOUNTER_SELECT, 0, mmGRBM_SE2_PERFCOUNTER_LO, mmGRBM_SE2_PERFCOUNTER_HI },
        { mmGRBM_SE3_PERFCOUNTER_SELECT, 0, mmGRBM_SE3_PERFCOUNTER_LO, mmGRBM_SE3_PERFCOUNTER_HI },
    }};

    // The RLC's SELECT registers are non-standard because they lack PERF_MODE fields. This should be fine though
    // because we only use PERFMON_COUNTER_MODE_ACCUM which is zero. If we ever try to use a different mode the RLC
    // needs to be handled as a special case.
    static_assert(PERFMON_COUNTER_MODE_ACCUM == 0, "RLC legacy counters need special handling.");

    PerfCounterBlockInfo*const pRlc = &pInfo->block[static_cast<uint32>(GpuBlock::Rlc)];
    pRlc->distribution              = PerfCounterDistribution::GlobalBlock;
    pRlc->numInstances              = 1;
    pRlc->numGenericSpmModules      = 0;
    pRlc->numGenericLegacyModules   = 2; // RLC_PERFCOUNTER0-1
    pRlc->numSpmWires               = 0;
    pRlc->maxEventId                = 6; // SERDES command write;

    pRlc->regAddr = { 0, {
        { mmRLC_PERFCOUNTER0_SELECT, 0, mmRLC_PERFCOUNTER0_LO, mmRLC_PERFCOUNTER0_HI },
        { mmRLC_PERFCOUNTER1_SELECT, 0, mmRLC_PERFCOUNTER1_LO, mmRLC_PERFCOUNTER1_HI },
    }};

    // The SDMA block has a unique programming model with 2 32-bit counters and unique registers for each instance.
    // All families except raven have 2 instances.
    PerfCounterBlockInfo*const pDma = &pInfo->block[static_cast<uint32>(GpuBlock::Dma)];
    pDma->distribution              = PerfCounterDistribution::GlobalBlock;
    pDma->numInstances              = (pProps->familyId != FAMILY_RV) ? 2 : 1;
    pDma->numGlobalOnlyCounters     = 2;
    pDma->numGenericSpmModules      = 0;
    pDma->numGenericLegacyModules   = 0;
    pDma->numSpmWires               = 0;
    pDma->maxEventId                = MaxSdmaPerfSelGfx09;

    pInfo->sdmaRegAddr[0][0] = { mmSDMA0_PERFMON_CNTL, 0, mmSDMA0_PERFCOUNTER0_RESULT, 0 };
    pInfo->sdmaRegAddr[0][1] = { mmSDMA0_PERFMON_CNTL, 0, mmSDMA0_PERFCOUNTER1_RESULT, 0 };

    if (pProps->familyId != FAMILY_RV)
    {
        pInfo->sdmaRegAddr[1][0] = { Vega::mmSDMA1_PERFMON_CNTL, 0, Vega::mmSDMA1_PERFCOUNTER0_RESULT, 0 };
        pInfo->sdmaRegAddr[1][1] = { Vega::mmSDMA1_PERFMON_CNTL, 0, Vega::mmSDMA1_PERFCOUNTER1_RESULT, 0 };
    }

    PerfCounterBlockInfo*const pCpg = &pInfo->block[static_cast<uint32>(GpuBlock::Cpg)];
    pCpg->distribution              = PerfCounterDistribution::GlobalBlock;
    pCpg->numInstances              = 1;
    pCpg->numGenericSpmModules      = 1; // CPG_PERFCOUNTER0
    pCpg->numGenericLegacyModules   = 1; // CPG_PERFCOUNTER1
    pCpg->numSpmWires               = 2;
    pCpg->spmBlockSelect            = Gfx9SpmGlobalBlockSelectCpg;
    pCpg->maxEventId                = Gfx9GetCpgMaxEventId(device);

    pCpg->regAddr = { 0, {
        { mmCPG_PERFCOUNTER0_SELECT, mmCPG_PERFCOUNTER0_SELECT1, mmCPG_PERFCOUNTER0_LO, mmCPG_PERFCOUNTER0_HI },
        { mmCPG_PERFCOUNTER1_SELECT, 0,                          mmCPG_PERFCOUNTER1_LO, mmCPG_PERFCOUNTER1_HI },
    }};

    PerfCounterBlockInfo*const pCpc = &pInfo->block[static_cast<uint32>(GpuBlock::Cpc)];
    pCpc->distribution              = PerfCounterDistribution::GlobalBlock;
    pCpc->numInstances              = 1;
    pCpc->numGenericSpmModules      = 1; // CPC_PERFCOUNTER0
    pCpc->numGenericLegacyModules   = 1; // CPC_PERFCOUNTER1
    pCpc->numSpmWires               = 2;
    pCpc->spmBlockSelect            = Gfx9SpmGlobalBlockSelectCpc;
    pCpc->maxEventId                = MaxCpcPerfcountSelGfx09;

    pCpc->regAddr = { 0, {
        { mmCPC_PERFCOUNTER0_SELECT, mmCPC_PERFCOUNTER0_SELECT1, mmCPC_PERFCOUNTER0_LO, mmCPC_PERFCOUNTER0_HI },
        { mmCPC_PERFCOUNTER1_SELECT, 0,                          mmCPC_PERFCOUNTER1_LO, mmCPC_PERFCOUNTER1_HI },
    }};

    PerfCounterBlockInfo*const pWd = &pInfo->block[static_cast<uint32>(GpuBlock::Wd)];
    pWd->distribution              = PerfCounterDistribution::GlobalBlock,
    pWd->numInstances              = 1;
    pWd->numGenericSpmModules      = 0;
    pWd->numGenericLegacyModules   = 4; // WD_PERFCOUNTER0-3
    pWd->numSpmWires               = 0;
    pWd->maxEventId                = MaxWdPerfcountSelect;

    pWd->regAddr = { 0, {
        { mmWD_PERFCOUNTER0_SELECT, 0, mmWD_PERFCOUNTER0_LO, mmWD_PERFCOUNTER0_HI },
        { mmWD_PERFCOUNTER1_SELECT, 0, mmWD_PERFCOUNTER1_LO, mmWD_PERFCOUNTER1_HI },
        { mmWD_PERFCOUNTER2_SELECT, 0, mmWD_PERFCOUNTER2_LO, mmWD_PERFCOUNTER2_HI },
        { mmWD_PERFCOUNTER3_SELECT, 0, mmWD_PERFCOUNTER3_LO, mmWD_PERFCOUNTER3_HI },
    }};

    PerfCounterBlockInfo*const pAtc = &pInfo->block[static_cast<uint32>(GpuBlock::Atc)];
    pAtc->distribution              = PerfCounterDistribution::GlobalBlock;
    pAtc->numInstances              = 1;
    pAtc->numGenericSpmModules      = 0;
    pAtc->numGenericLegacyModules   = 4; // ATC_PERFCOUNTER0-3
    pAtc->numSpmWires               = 0;
    pAtc->maxEventId                = 23;
    pAtc->isCfgStyle                = true;

    pAtc->regAddr = { mmATC_PERFCOUNTER_RSLT_CNTL, {
        { mmATC_PERFCOUNTER0_CFG, 0, mmATC_PERFCOUNTER_LO, mmATC_PERFCOUNTER_HI },
        { mmATC_PERFCOUNTER1_CFG, 0, mmATC_PERFCOUNTER_LO, mmATC_PERFCOUNTER_HI },
        { mmATC_PERFCOUNTER2_CFG, 0, mmATC_PERFCOUNTER_LO, mmATC_PERFCOUNTER_HI },
        { mmATC_PERFCOUNTER3_CFG, 0, mmATC_PERFCOUNTER_LO, mmATC_PERFCOUNTER_HI },
    }};

    PerfCounterBlockInfo*const pAtcL2 = &pInfo->block[static_cast<uint32>(GpuBlock::AtcL2)];
    pAtcL2->distribution              = PerfCounterDistribution::GlobalBlock;
    pAtcL2->numInstances              = 1;
    pAtcL2->numGenericSpmModules      = 0;
    pAtcL2->numGenericLegacyModules   = 2; // ATC_L2_PERFCOUNTER0-1
    pAtcL2->numSpmWires               = 0;
    pAtcL2->maxEventId                = 8;
    pAtcL2->isCfgStyle                = true;

    pAtcL2->regAddr = { mmATC_L2_PERFCOUNTER_RSLT_CNTL, {
        { mmATC_L2_PERFCOUNTER0_CFG, 0, mmATC_L2_PERFCOUNTER_LO, mmATC_L2_PERFCOUNTER_HI },
        { mmATC_L2_PERFCOUNTER1_CFG, 0, mmATC_L2_PERFCOUNTER_LO, mmATC_L2_PERFCOUNTER_HI },
    }};

    PerfCounterBlockInfo*const pMcVmL2 = &pInfo->block[static_cast<uint32>(GpuBlock::McVmL2)];
    pMcVmL2->distribution              = PerfCounterDistribution::GlobalBlock;
    pMcVmL2->numInstances              = 1;
    pMcVmL2->numGenericSpmModules      = 0;
    pMcVmL2->numGenericLegacyModules   = 8; // MC_VM_L2_PERFCOUNTER0-7
    pMcVmL2->numSpmWires               = 0;
    pMcVmL2->maxEventId                = 20; // Number of l2 cache invalidations
    pMcVmL2->isCfgStyle                = true;

    pMcVmL2->regAddr = { mmMC_VM_L2_PERFCOUNTER_RSLT_CNTL, {
        { mmMC_VM_L2_PERFCOUNTER0_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER1_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER2_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER3_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER4_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER5_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER6_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
        { mmMC_VM_L2_PERFCOUNTER7_CFG, 0, mmMC_VM_L2_PERFCOUNTER_LO, mmMC_VM_L2_PERFCOUNTER_HI },
    }};

    PerfCounterBlockInfo*const pEa = &pInfo->block[static_cast<uint32>(GpuBlock::Ea)];
    pEa->distribution              = PerfCounterDistribution::GlobalBlock;
    pEa->numInstances              = 16; // This probably isn't true for all ASICs.
    pEa->numGenericSpmModules      = 0;
    pEa->numGenericLegacyModules   = 2; // EA_PERFCOUNTER0-1
    pEa->numSpmWires               = 0;
    pEa->maxEventId                = 76; // | mam | {3`b0, alog_cache_hit}
    pEa->isCfgStyle                = true;

    if (IsVega10(device) || IsRaven(device))
    {
        pEa->regAddr = { Gfx09_0::mmGCEA_PERFCOUNTER_RSLT_CNTL, {
            { Gfx09_0::mmGCEA_PERFCOUNTER0_CFG, 0, Gfx09_0::mmGCEA_PERFCOUNTER_LO, Gfx09_0::mmGCEA_PERFCOUNTER_HI },
            { Gfx09_0::mmGCEA_PERFCOUNTER1_CFG, 0, Gfx09_0::mmGCEA_PERFCOUNTER_LO, Gfx09_0::mmGCEA_PERFCOUNTER_HI },
        }};
    }
    else
    {
        pEa->regAddr = { Gfx09_1x::mmGCEA_PERFCOUNTER_RSLT_CNTL, {
            { Gfx09_1x::mmGCEA_PERFCOUNTER0_CFG, 0, Gfx09_1x::mmGCEA_PERFCOUNTER_LO, Gfx09_1x::mmGCEA_PERFCOUNTER_HI },
            { Gfx09_1x::mmGCEA_PERFCOUNTER1_CFG, 0, Gfx09_1x::mmGCEA_PERFCOUNTER_LO, Gfx09_1x::mmGCEA_PERFCOUNTER_HI },
        }};
    }

    PerfCounterBlockInfo*const pRpb = &pInfo->block[static_cast<uint32>(GpuBlock::Rpb)];
    pRpb->distribution              = PerfCounterDistribution::GlobalBlock;
    pRpb->numInstances              = 1;
    pRpb->numGenericSpmModules      = 0;
    pRpb->numGenericLegacyModules   = 4; // RPB_PERFCOUNTER0-3
    pRpb->numSpmWires               = 0;
    pRpb->maxEventId                = 63;
    pRpb->isCfgStyle                = true;

    // Sets the register addresses.
    Gfx9UpdateRpbBlockInfo(device, pRpb);

    // The RMI is very odd. It looks like it uses the generic programming model but it interleaves legacy modules
    // and SPM modules. It also only has 2 SPM wires so it can't use more than one SPM module anyway.
    //
    // Digging further, counters 0 and 1 only count the left half of the RMI (RMI0) and counters 2 and 3 only count
    // the right half. There is a special control register which manages some of this state including which side
    // sends SPM data back to the RLC.
    //
    // This doesn't really fit our perf experiment interface. For now we will just treat it as one SPM module
    // for RMI0 and three legacy modules. The user has to deal with the RMI0/RMI1 split themselves.
    //
    PerfCounterBlockInfo*const pRmi = &pInfo->block[static_cast<uint32>(GpuBlock::Rmi)];
    pRmi->distribution              = PerfCounterDistribution::PerShaderArray;
    pRmi->numInstances              = 2;
    pRmi->numGenericSpmModules      = 1; // RMI_PERFCOUNTER0
    pRmi->numGenericLegacyModules   = 3; // RMI_PERFCOUNTER1-3
    pRmi->numSpmWires               = 2;
    pRmi->spmBlockSelect            = Gfx9SpmSeBlockSelectRmi;
    pRmi->maxEventId                = MaxRMIPerfSelGfx09;

    pRmi->regAddr = { 0, {
        { mmRMI_PERFCOUNTER0_SELECT, mmRMI_PERFCOUNTER0_SELECT1, mmRMI_PERFCOUNTER0_LO, mmRMI_PERFCOUNTER0_HI },
        { mmRMI_PERFCOUNTER1_SELECT, 0,                          mmRMI_PERFCOUNTER1_LO, mmRMI_PERFCOUNTER1_HI },
        { mmRMI_PERFCOUNTER2_SELECT, 0,                          mmRMI_PERFCOUNTER2_LO, mmRMI_PERFCOUNTER2_HI },
        { mmRMI_PERFCOUNTER3_SELECT, 0,                          mmRMI_PERFCOUNTER3_LO, mmRMI_PERFCOUNTER3_HI },
    }};

    // The UMCCH has a unique programming model. It defines a fixed number of global counters for each instance.
    PerfCounterBlockInfo*const pUmcch = &pInfo->block[static_cast<uint32>(GpuBlock::Umcch)];
    pUmcch->distribution              = PerfCounterDistribution::GlobalBlock;
    pUmcch->numGlobalOnlyCounters     = Gfx9MaxUmcchPerfModules;
    pUmcch->numGenericSpmModules      = 0;
    pUmcch->numGenericLegacyModules   = 0;
    pUmcch->numSpmWires               = 0;
    pUmcch->maxEventId                = 39; // BeqEdcErr

    // Note that this function also sets numInstances.
    UpdateUmcchBlockInfo(device, pInfo, pUmcch);

    // A quick check to make sure we have registers for all instances. The fact that the number of instances varies
    // per ASIC doesn't mesh well with our register header scheme. If this triggers UpdateUmcchBlockInfo needs fixing.
    PAL_ASSERT(pInfo->umcchRegAddr[pUmcch->numInstances - 1].perfMonCtlClk != 0);
}

// =====================================================================================================================
// Initializes the performance counter information for an adapter structure, specifically for the Gfx9 hardware layer.
void InitPerfCtrInfo(
    const Pal::Device& device,
    GpuChipProperties* pProps)
{
    // Something pretty terrible will probably happen if this isn't true.
    PAL_ASSERT(pProps->gfx9.numShaderEngines <= Gfx9MaxShaderEngines);

    Gfx9PerfCounterInfo*const pInfo = &pProps->gfx9.perfCounterInfo;

    // The caller should already have zeroed this struct a long time ago but let's do it again just to be sure.
    // We depend very heavily on unsupported fields being zero by default.
    memset(pInfo, 0, sizeof(*pInfo));

    // The SPM block select requires a non-zero default. We use UINT32_MAX to indicate "invalid".
    for (uint32 idx = 0; idx < static_cast<uint32>(GpuBlock::Count); idx++)
    {
        pInfo->block[idx].spmBlockSelect = UINT32_MAX;
    }

    // These features are supported by all ASICs.
    pInfo->features.counters         = 1;
    pInfo->features.threadTrace      = 1;
    pInfo->features.spmTrace         = 1;
    pInfo->features.supportPs1Events = 1;

    // Set the hardware specified per-block information (see the function for what exactly that means).
    // There's so much code to do this that it had to go in a helper function for each version.
    if (pProps->gfxLevel == GfxIpLevel::GfxIp9)
    {
        Gfx9InitBasicBlockInfo(device, pProps);
    }

    // Using that information, infer the remaining per-block properties.
    for (uint32 idx = 0; idx < static_cast<uint32>(GpuBlock::Count); idx++)
    {
        PerfCounterBlockInfo*const pBlock = &pInfo->block[idx];

        if (pBlock->distribution != PerfCounterDistribution::Unavailable)
        {
            // Compute the total instance count.
            if (pBlock->distribution == PerfCounterDistribution::PerShaderArray)
            {
                pBlock->numGlobalInstances =
                    pBlock->numInstances * pProps->gfx9.numShaderEngines * pProps->gfx9.numShaderArrays;
            }
            else if (pBlock->distribution == PerfCounterDistribution::PerShaderEngine)
            {
                pBlock->numGlobalInstances = pBlock->numInstances * pProps->gfx9.numShaderEngines;
            }
            else
            {
                pBlock->numGlobalInstances = pBlock->numInstances;
            }

            // If this triggers we need to increase MaxPerfModules.
            const uint32 totalGenericModules = pBlock->numGenericSpmModules + pBlock->numGenericLegacyModules;
            PAL_ASSERT(totalGenericModules <= MaxPerfModules);

            // These are a fairly simple translation for the generic blocks. The blocks that require special treatment
            // must set the generic module counts to zero and manually set their numbers of counters.
            if (totalGenericModules > 0)
            {
                PAL_ASSERT((pBlock->num16BitSpmCounters == 0) && (pBlock->num32BitSpmCounters == 0) &&
                           (pBlock->numGlobalOnlyCounters == 0) && (pBlock->numGlobalSharedCounters == 0));

                pBlock->num16BitSpmCounters     = pBlock->numGenericSpmModules * 4;
                pBlock->num32BitSpmCounters     = pBlock->numGenericSpmModules * 2;
                pBlock->numGlobalOnlyCounters   = pBlock->numGenericLegacyModules;
                pBlock->numGlobalSharedCounters = pBlock->numGenericSpmModules;
            }

            // If some block has SPM counters it must have SPM wires and a SPM block select.
            PAL_ASSERT(((pBlock->num16BitSpmCounters == 0) && (pBlock->num32BitSpmCounters == 0)) ||
                       ((pBlock->numSpmWires > 0) && (pBlock->spmBlockSelect != UINT32_MAX)));
        }
    }

    // Verify that we didn't exceed any of our hard coded per-block constants.
    PAL_ASSERT(pInfo->block[static_cast<uint32>(GpuBlock::Dma)].numGlobalInstances     <= Gfx9MaxSdmaInstances);
    PAL_ASSERT(pInfo->block[static_cast<uint32>(GpuBlock::Dma)].numGenericSpmModules   <= Gfx9MaxSdmaPerfModules);
    PAL_ASSERT(pInfo->block[static_cast<uint32>(GpuBlock::Umcch)].numGlobalInstances   <= Gfx9MaxUmcchInstances);
}

} // Gfx9
} // Pal
