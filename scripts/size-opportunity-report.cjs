#!/usr/bin/env node

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

function usage() {
  console.error(`Usage: node scripts/size-opportunity-report.cjs [options] [file-or-dir...]

Options:
  --compiler PATH   mkpro-native binary to run (default: native/build/mkpro-native)
  --json            Print the raw aggregate as JSON
  --summary-only    Print only aggregate actions and blocker groups
  --limit N         Analyze at most N input files
  --help            Show this help

With no file-or-dir arguments, the script analyzes examples/ and
examples/pending-optimizer/.`);
}

function parseArgs(argv) {
  const options = {
    compiler: 'native/build/mkpro-native',
    json: false,
    summaryOnly: false,
    limit: null,
    inputs: [],
  };
  for (let index = 0; index < argv.length; ++index) {
    const arg = argv[index];
    if (arg === '--help' || arg === '-h') {
      usage();
      process.exit(0);
    }
    if (arg === '--json') {
      options.json = true;
      continue;
    }
    if (arg === '--summary-only') {
      options.summaryOnly = true;
      continue;
    }
    if (arg === '--compiler') {
      if (index + 1 >= argv.length) {
        throw new Error('missing value for --compiler');
      }
      options.compiler = argv[++index];
      continue;
    }
    if (arg === '--limit') {
      if (index + 1 >= argv.length) {
        throw new Error('missing value for --limit');
      }
      const limit = Number(argv[++index]);
      if (!Number.isInteger(limit) || limit < 1) {
        throw new Error('--limit must be a positive integer');
      }
      options.limit = limit;
      continue;
    }
    if (arg.startsWith('--')) {
      throw new Error(`unknown option: ${arg}`);
    }
    options.inputs.push(arg);
  }
  return options;
}

function collectMkproFiles(inputPath) {
  const stat = fs.statSync(inputPath);
  if (stat.isFile()) {
    return inputPath.endsWith('.mkpro') ? [inputPath] : [];
  }
  if (!stat.isDirectory()) {
    return [];
  }
  return fs.readdirSync(inputPath)
    .map((name) => path.join(inputPath, name))
    .filter((file) => fs.statSync(file).isFile() && file.endsWith('.mkpro'));
}

function defaultInputs() {
  return ['examples', path.join('examples', 'pending-optimizer')];
}

function compileJson(compiler, file) {
  const raw = execFileSync(
    compiler,
    ['compile', file, '--analysis', '--out', 'json'],
    { encoding: 'utf8', maxBuffer: 128 * 1024 * 1024 },
  );
  return JSON.parse(raw);
}

function sizeAttribution(result) {
  return result.report?.sizeAttribution ?? result.sizeAttribution ?? null;
}

function detail(details, key) {
  return details?.[key] ?? '';
}

function addGrouped(map, key, row) {
  const group = map.get(key) ?? {
    key,
    files: 0,
    opportunities: 0,
    potentialSavings: 0,
    bestSavings: Number.NEGATIVE_INFINITY,
    best: null,
  };
  group.files += row.firstInGroup ? 1 : 0;
  group.opportunities += row.opportunities ?? 1;
  group.potentialSavings += Math.max(0, row.potentialSavings ?? row.savings ?? 0);
  const bestSavings = row.bestSavings ?? row.savings ?? 0;
  if (bestSavings > group.bestSavings) {
    group.bestSavings = bestSavings;
    group.best = row;
  }
  map.set(key, group);
}

function analyzeFile(compiler, file) {
  const result = compileJson(compiler, file);
  const report = sizeAttribution(result);
  if (!report) {
    throw new Error(`compiler JSON did not include sizeAttribution for ${file}`);
  }
  const helpers = (report.helpers ?? []).map((helper) => ({
    file,
    label: helper.label ?? '',
    totalCells: Number(helper.totalCells ?? 0),
    bodyCells: Number(helper.bodyCells ?? 0),
    callSiteCells: Number(helper.callSiteCells ?? 0),
    callOccurrences: Number(helper.callOccurrences ?? 0),
    bodyOccurrences: Number(helper.bodyOccurrences ?? 0),
    firstAddress: helper.firstAddress ?? '',
    lastAddress: helper.lastAddress ?? '',
    role: detail(helper.details, 'role'),
    pipelineShape: detail(helper.details, 'pipelineShape'),
    bodyCellsPerAccumulatorTerm: detail(helper.details, 'bodyCellsPerAccumulatorTerm'),
    accumulatorStatePolicy: detail(helper.details, 'accumulatorStatePolicy'),
    interleavedPipelineShape: detail(helper.details, 'interleavedPipelineShape'),
    interleavedPipelineBlocker: detail(helper.details, 'interleavedPipelineBlocker'),
    callCellsPerOccurrence: detail(helper.details, 'callCellsPerOccurrence'),
    nextPipelineAction: detail(helper.details, 'nextPipelineAction'),
    nextPipelineProofTarget: detail(helper.details, 'nextPipelineProofTarget'),
    valueAwareArgumentRecallSites: detail(
      helper.details,
      'argumentRecallSitesByName',
    ) || detail(
      helper.details,
      'valueAwareArgumentRecallSitesByName',
    ),
    valueAwareRepeatedArgumentRecallSites: detail(
      helper.details,
      'repeatedArgumentRecallSites',
    ) || detail(
      helper.details,
      'valueAwareRepeatedArgumentRecallSites',
    ),
    valueAwareTopRepeatedArgumentRecall: detail(
      helper.details,
      'topRepeatedArgumentRecall',
    ) || detail(
      helper.details,
      'valueAwareTopRepeatedArgumentRecall',
    ),
    valueAwareSchedulerNextMaterializationTarget: detail(
      helper.details,
      'schedulerNextMaterializationTarget',
    ) || detail(
      helper.details,
      'valueAwareSchedulerNextMaterializationTarget',
    ),
    valueAwareRepeatedArgumentSchedulerFeasibility: detail(
      helper.details,
      'repeatedArgumentSchedulerFeasibility',
    ) || detail(
      helper.details,
      'valueAwareRepeatedArgumentSchedulerFeasibility',
    ),
    valueAwareRepeatedArgumentSchedulerStatus: detail(
      helper.details,
      'repeatedArgumentSchedulerStatus',
    ) || detail(
      helper.details,
      'valueAwareRepeatedArgumentSchedulerStatus',
    ),
    valueAwareRepeatedArgumentSchedulerAction: detail(
      helper.details,
      'repeatedArgumentSchedulerAction',
    ) || detail(
      helper.details,
      'valueAwareRepeatedArgumentSchedulerAction',
    ),
    valueAwareRepeatedArgumentSchedulerProfitableNet: detail(
      helper.details,
      'repeatedArgumentSchedulerProfitableNetCells',
    ) || detail(
      helper.details,
      'valueAwareRepeatedArgumentSchedulerProfitableNetCells',
    ),
    valueAwareRepeatedArgumentSchedulerModelNet: detail(
      helper.details,
      'repeatedArgumentSchedulerModelNetCells',
    ) || detail(
      helper.details,
      'valueAwareRepeatedArgumentSchedulerModelNetCells',
    ),
    selectedStackCarriedPlan:
      detail(helper.details, 'valueAwareSelectedStackCarriedPlan'),
    selectedStackCarriedStatus:
      detail(helper.details, 'valueAwareSelectedStackCarriedStatus'),
    selectedStackCarriedInputs:
      detail(helper.details, 'valueAwareSelectedStackCarriedInputNames'),
    selectedStackCarriedTargets:
      detail(helper.details, 'valueAwareSelectedStackCarriedTargets'),
    selectedStackCarriedSites:
      detail(helper.details, 'valueAwareSelectedStackCarriedSites'),
    symbolicKnownCalleeStackEffects:
      detail(helper.details, 'valueAwareSymbolicKnownCalleeStackEffects'),
    details: helper.details ?? {},
  }));
  const candidateOpportunities = [];
  const helperTraffic = [];
  for (const opportunity of report.opportunities ?? []) {
    if (opportunity.variant !== 'helper-register-traffic') {
      const details = opportunity.details ?? {};
      candidateOpportunities.push({
        file,
        variant: opportunity.variant ?? '',
        site: opportunity.site ?? '',
        savings: Number(opportunity.savings ?? 0),
        currentSteps: Number(opportunity.currentSteps ?? 0),
        candidateSteps: Number(opportunity.candidateSteps ?? 0),
        blockerKind: opportunity.blockerKind ?? '',
        reason: opportunity.reason ?? '',
        requiredAction: detail(details, 'requiredAction'),
        proofStatus: detail(details, 'proofStatus'),
        proofDisposition: detail(details, 'proofDisposition') ||
          detail(details, 'blockedProof'),
        sizeImpactStatus: detail(details, 'sizeImpactStatus'),
        netSavingsStatus: detail(details, 'netSavingsStatus'),
        candidateStepsStatus: detail(details, 'candidateStepsStatus'),
        estimateKind: detail(details, 'estimateKind'),
        proofEffortPriority: detail(details, 'proofEffortPriority'),
        proofEffortReason: detail(details, 'proofEffortReason'),
        proofEffortSizeGate: detail(details, 'proofEffortSizeGate'),
        sizeFirstAction: detail(details, 'sizeFirstAction'),
        layoutProofScope: detail(details, 'layoutProofScope'),
        layoutProofStatus: detail(details, 'layoutProofStatus'),
        layoutProofRequirement: detail(details, 'layoutProofRequirement'),
        layoutProofRequiredArtifact: detail(
          details,
          'layoutProofRequiredArtifact',
        ),
        layoutProofConflictModel: detail(details, 'layoutProofConflictModel'),
        layoutProofNextAction: detail(details, 'layoutProofNextAction'),
        fractionalSelectorConsumer: detail(details, 'fractionalSelectorConsumer'),
        fractionalSelectorSource: detail(details, 'fractionalSelectorSource'),
        fractionalSelectorStorage: detail(details, 'fractionalSelectorStorage'),
        selectorTarget: detail(details, 'selectorTarget'),
        consumerAddress: detail(details, 'consumerAddress'),
        consumerControlKind: detail(details, 'consumerControlKind'),
        integerPartUseRole: detail(details, 'integerPartUseRole'),
        fractionalPartUseRole: detail(details, 'fractionalPartUseRole'),
        integerPartDataSafetyStatus: detail(details, 'integerPartDataSafetyStatus'),
        deadIntegerProofGap: detail(details, 'deadIntegerProofGap'),
        deadIntegerProofRequiredArtifact:
          detail(details, 'deadIntegerProofRequiredArtifact'),
        deadIntegerProofNextAction: detail(details, 'deadIntegerProofNextAction'),
        deadIntegerConsumerRegister: detail(details, 'deadIntegerConsumerRegister'),
        deadIntegerSelectorCarrierRegister: detail(
          details,
          'deadIntegerSelectorCarrierRegister',
        ),
        fractionalSelectorSourceRegister: detail(
          details,
          'fractionalSelectorSourceRegister',
        ),
        stackResidentEntryFamily: detail(details, 'stackResidentEntryFamily'),
        stackResidentEntryDelta: detail(details, 'stackResidentEntryDeltaCells'),
        stackResidentEntryMeasuredStatus: detail(
          details,
          'stackResidentEntryMeasuredStatus',
        ),
        stackResidentEntryAbiContext: detail(details, 'stackResidentEntryAbiContext'),
        stackResidentEntryValueAwareContext: detail(
          details,
          'stackResidentEntryValueAwareContext',
        ),
        stackResidentEntryRequiredAction: detail(
          details,
          'stackResidentEntryRequiredAction',
        ),
        details,
      });
      continue;
    }
    const details = opportunity.details ?? {};
    helperTraffic.push({
      file,
      helper: detail(details, 'helperLabel'),
      savings: opportunity.savings,
      currentSteps: opportunity.currentSteps,
      candidateSteps: opportunity.candidateSteps,
      registerTrafficCells: Number(detail(details, 'registerTrafficCells') || 0),
      registerTrafficNames: detail(details, 'registerTrafficNames'),
      valueAwareArgumentRecallSites: detail(
        details,
        'argumentRecallSitesByName',
      ) || detail(
        details,
        'valueAwareArgumentRecallSitesByName',
      ),
      valueAwareRepeatedArgumentRecallSites: detail(
        details,
        'repeatedArgumentRecallSites',
      ) || detail(
        details,
        'valueAwareRepeatedArgumentRecallSites',
      ),
      valueAwareTopRepeatedArgumentRecall: detail(
        details,
        'topRepeatedArgumentRecall',
      ) || detail(
        details,
        'valueAwareTopRepeatedArgumentRecall',
      ),
      valueAwareSchedulerNextMaterializationTarget: detail(
        details,
        'schedulerNextMaterializationTarget',
      ) || detail(
        details,
        'valueAwareSchedulerNextMaterializationTarget',
      ),
      valueAwareRepeatedArgumentSchedulerFeasibility: detail(
        details,
        'repeatedArgumentSchedulerFeasibility',
      ) || detail(
        details,
        'valueAwareRepeatedArgumentSchedulerFeasibility',
      ),
      valueAwareRepeatedArgumentSchedulerStatus: detail(
        details,
        'repeatedArgumentSchedulerStatus',
      ) || detail(
        details,
        'valueAwareRepeatedArgumentSchedulerStatus',
      ),
      valueAwareRepeatedArgumentSchedulerAction: detail(
        details,
        'repeatedArgumentSchedulerAction',
      ) || detail(
        details,
        'valueAwareRepeatedArgumentSchedulerAction',
      ),
      valueAwareRepeatedArgumentSchedulerProfitableNet: detail(
        details,
        'repeatedArgumentSchedulerProfitableNetCells',
      ) || detail(
        details,
        'valueAwareRepeatedArgumentSchedulerProfitableNetCells',
      ),
      valueAwareRepeatedArgumentSchedulerModelNet: detail(
        details,
        'repeatedArgumentSchedulerModelNetCells',
      ) || detail(
        details,
        'valueAwareRepeatedArgumentSchedulerModelNetCells',
      ),
      trafficShape: detail(details, 'valueAwareSchedulerTrafficShape'),
      planStatus:
        detail(details, 'valueAwareSchedulerPlanStatus') ||
        detail(details, 'valueAwareStackInputPlanStatus') ||
        detail(details, 'valueAwareProfitableStackInputPlanStatus'),
      requiredAction: detail(details, 'requiredAction'),
      costModelAction: detail(details, 'costModelAction'),
      trafficShapeAction: detail(details, 'trafficShapeAction'),
      estimatedNet: detail(details, 'valueAwareEstimatedNetSavingsAfterMaterialization'),
      estimatedModel: detail(details, 'valueAwareEstimatedNetSavingsModel'),
      profitableInputs: detail(details, 'valueAwareProfitableStackInputNames'),
      breakEvenInputs: detail(details, 'valueAwareBreakEvenStackInputNames'),
      breakEvenInputCount: detail(details, 'valueAwareBreakEvenStackInputCount'),
      breakEvenInputNet: detail(details, 'valueAwareBreakEvenStackInputNetCells'),
      breakEvenInputNeed: detail(
        details,
        'valueAwareBreakEvenStackInputAdditionalNetCellsToPositive',
      ),
      breakEvenInputGapReason: detail(
        details,
        'valueAwareBreakEvenStackInputPositiveGapReason',
      ),
      breakEvenInputNextProofTarget: detail(
        details,
        'valueAwareBreakEvenStackInputNextProofTarget',
      ),
      breakEvenInputRequiredAction: detail(
        details,
        'valueAwareBreakEvenStackInputRequiredAction',
      ),
      unprofitableInputs: detail(details, 'valueAwareUnprofitableStackInputNames'),
      suggestedResidentInputs: detail(details, 'valueAwareSuggestedResidentInputNames'),
      profitBreakdown: detail(details, 'valueAwareStackInputProfitBreakdown'),
      materializeCellsByName: detail(details, 'valueAwareStackInputMaterializeCellsByName'),
      currentXMaterializeCellsByName:
        detail(details, 'valueAwareCurrentXStackInputMaterializeCellsByName'),
      currentXMaterializeSites:
        detail(details, 'valueAwareCurrentXStackInputMaterializeSites'),
      currentXRetainedStores:
        detail(details, 'valueAwareCurrentXStackInputRetainedStoreCellsByName'),
      currentXRetainedSites:
        detail(details, 'valueAwareCurrentXStackInputRetainedStoreSites'),
      currentXRetainedReasons:
        detail(details, 'valueAwareCurrentXStackInputRetainedStoreReasons'),
      currentXMaterializeProof:
        detail(details, 'valueAwareCurrentXStackInputMaterializeProofStatus'),
      currentXMaterializeAction:
        detail(details, 'valueAwareCurrentXStackInputMaterializeRequiredAction'),
      callerArgStoreCells: detail(details, 'valueAwareCallArgumentStoreCells'),
      callerArgStoreCellsByName: detail(details, 'valueAwareCallArgumentStoreCellsByName'),
      callerArgStoreAdjustedNet: detail(
        details,
        'valueAwareCallerArgStoreAdjustedStackInputNetCells',
      ),
      callerArgStorePlanStatus: detail(details, 'valueAwareCallerArgStorePlanStatus'),
      callerArgStoreRequiredAction: detail(details, 'valueAwareCallerArgStoreRequiredAction'),
      callArgumentPreservationCellsByCallee: detail(
        details,
        'valueAwareCallArgumentPreservationCellsByCallee',
      ),
      callArgumentPreservationRaw: detail(
        details,
        'valueAwareCallArgumentPreservationRawCells',
      ),
      callArgumentPreservationZeroCopy: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopyCells',
      ),
      callArgumentPreservationZeroCopyStatus: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopyStatus',
      ),
      callArgumentPreservationZeroCopySites: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopySites',
      ),
      callArgumentPreservationZeroCopyBlockers: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopyBlockers',
      ),
      callArgumentPreservationLowerBound: detail(
        details,
        'valueAwareCallArgumentPreservationLowerBoundCells',
      ),
      callArgumentPreservationLowerBoundBasis: detail(
        details,
        'valueAwareCallArgumentPreservationLowerBoundBasis',
      ),
      callArgumentPreservationRequiredAction: detail(
        details,
        'valueAwareCallArgumentPreservationRequiredAction',
      ),
      callArgumentX2RestoreStatus: detail(details, 'valueAwareCallArgumentX2RestoreStatus'),
      callArgumentX2MutationOpcodesByCallee: detail(
        details,
        'valueAwareCallArgumentX2MutationOpcodesByCallee',
      ),
      callArgumentX2ClobberClassesByCallee: detail(
        details,
        'valueAwareCallArgumentX2ClobberClassesByCallee',
      ),
      callArgumentX2PreloadConstantOpcodesByCallee: detail(
        details,
        'valueAwareCallArgumentX2PreloadConstantOpcodesByCallee',
      ),
      callArgumentX2PreloadLiteralReplacementByCallee: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementByCallee',
      ),
      callArgumentX2PreloadLiteralReplacementStatus: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementStatus',
      ),
      callArgumentX2PreloadLiteralReplacementDelta: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementDeltaCells',
      ),
      callArgumentX2PreloadLiteralReplacementNet: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementHypotheticalNetCells',
      ),
      callArgumentX2PreloadRefactorRequiredAction: detail(
        details,
        'valueAwareCallArgumentX2PreloadRefactorRequiredAction',
      ),
      callArgumentX2RequiredAction: detail(
        details,
        'valueAwareCallArgumentX2RequiredAction',
      ),
      callArgumentSites: detail(details, 'valueAwareCallArgumentSites'),
      callArgumentInputNamesByCallee: detail(
        details,
        'valueAwareCallArgumentInputNamesByCallee',
      ),
      callPreservationSites: detail(details, 'valueAwareCallPreservationSites'),
      symbolicEntryStackByCallSite: detail(
        details,
        'valueAwareSymbolicEntryStackByCallSite',
      ),
      symbolicEntryStackSeed: detail(details, 'valueAwareSymbolicEntryStackSeed'),
      symbolicEntryStackSeedStatus: detail(
        details,
        'valueAwareSymbolicEntryStackSeedStatus',
      ),
      symbolicKnownCalleeStackEffects: detail(
        details,
        'valueAwareSymbolicKnownCalleeStackEffects',
      ),
      symbolicFlowEntryStack: detail(details, 'valueAwareSymbolicFlowEntryStack'),
      entryStackLostKnownFacts: detail(
        details,
        'valueAwareEntryStackLostKnownFacts',
      ),
      entryStackLostKnownFactCount: detail(
        details,
        'valueAwareEntryStackLostKnownFactCount',
      ),
      entryStackLostKnownFactSlots: detail(
        details,
        'valueAwareEntryStackLostKnownFactSlots',
      ),
      entryStackLostKnownFactStatus: detail(
        details,
        'valueAwareEntryStackLostKnownFactStatus',
      ),
      entryStackLostKnownFactTarget: detail(
        details,
        'valueAwareEntryStackLostKnownFactNextProofTarget',
      ),
      entryStackLostKnownFactAction: detail(
        details,
        'valueAwareEntryStackLostKnownFactRequiredAction',
      ),
      existingEntryStackInputSites: detail(
        details,
        'valueAwareExistingEntryStackInputSites',
      ),
      existingEntryStackInputSitesByName: detail(
        details,
        'valueAwareExistingEntryStackInputSitesByName',
      ),
      selectedStackCarriedPlan: detail(
        details,
        'valueAwareSelectedStackCarriedPlan',
      ),
      selectedStackCarriedStatus: detail(
        details,
        'valueAwareSelectedStackCarriedStatus',
      ),
      selectedStackCarriedInputs: detail(
        details,
        'valueAwareSelectedStackCarriedInputNames',
      ),
      selectedStackCarriedTargets: detail(
        details,
        'valueAwareSelectedStackCarriedTargets',
      ),
      selectedStackCarriedSites: detail(
        details,
        'valueAwareSelectedStackCarriedSites',
      ),
      callPreservationCalleeX2Effects: detail(
        details,
        'valueAwareCallPreservationCalleeX2Effects',
      ),
      callPreservationMutatingOpcodes:
        detail(details, 'valueAwareCallPreservationMutatingOpcodes'),
      directMaterialization: detail(details, 'valueAwareDirectStackInputMaterialization'),
      directMaterializationStatus: detail(
        details,
        'valueAwareDirectStackInputMaterializationStatus',
      ),
      bestInput: detail(details, 'valueAwareBestStackInputCandidate'),
      bestInputNet: detail(details, 'valueAwareBestStackInputNetCells'),
      bestInputAdditionalRecallCellsToProfit:
        detail(details, 'valueAwareBestStackInputAdditionalRecallCellsToProfit'),
      bestInputGapReason: detail(details, 'valueAwareBestStackInputPositiveGapReason'),
      bestInputNextProofTarget: detail(details, 'valueAwareBestStackInputNextProofTarget'),
      calleeStackSurvival: detail(details, 'valueAwareCallPreservationCalleeStackSurvival'),
      calleeNaturalPreservedSlots: detail(
        details,
        'valueAwareCalleeAbiNaturalPreservedSlotsByCallee',
      ),
      calleeNaturalPreservedSlotFinalSlots: detail(
        details,
        'valueAwareCalleeAbiNaturalPreservedSlotFinalSlotsByCallee',
      ),
      calleeNaturalRestoreCells: detail(
        details,
        'valueAwareCalleeAbiNaturalPreservedSlotRestoreCellsByCallee',
      ),
      calleeNaturalMinRestoreCells: detail(
        details,
        'valueAwareCalleeAbiNaturalPreserveMinRestoreCellsByCallee',
      ),
      calleeNaturalFirstRecallCoverage: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallCoverageByCallee',
      ),
      calleeNaturalFirstRecallStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallCoverageStatusByCallee',
      ),
      calleeNaturalFirstRecallChoice: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceByCallee',
      ),
      calleeNaturalFirstRecallChoiceStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceStatusByCallee',
      ),
      calleeNaturalFirstRecallUseShape: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallUseShapeByCallee',
      ),
      calleeNaturalFirstRecallUseShapeStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallUseShapeStatus',
      ),
      calleeNaturalFirstRecallUseShapeAction: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallUseShapeAction',
      ),
      calleeNaturalFirstRecallChoiceSearch: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceSearchByCallee',
      ),
      calleeNaturalFirstRecallChoiceSearchStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceSearchStatusByCallee',
      ),
      calleeRemainingPreserveDepth: detail(
        details,
        'valueAwareCalleeAbiRemainingPreserveDepthByCallee',
      ),
      calleeAbiPureStackPlacement: detail(
        details,
        'valueAwareCalleeAbiPureStackPlacementByCallee',
      ),
      calleeAbiPureStackPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPureStackPlacementStatus',
      ),
      calleeAbiPrimaryEntryProofDisposition: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProofDisposition',
      ),
      calleeAbiNetAfterLowerBound: detail(
        details,
        'valueAwareCalleeAbiNetAfterLowerBoundCells',
      ),
      calleeAbiAdditionalNetToPositive: detail(
        details,
        'valueAwareCalleeAbiAdditionalNetCellsToPositive',
      ),
      calleeAbiCostBreakdown: detail(details, 'valueAwareCalleeAbiCostBreakdown'),
      calleeAbiPositiveLevers: detail(details, 'valueAwareCalleeAbiPositiveLevers'),
      calleeAbiPositiveGapReason: detail(
        details,
        'valueAwareCalleeAbiPositiveGapReason',
      ),
      calleeAbiNextProofTarget: detail(details, 'valueAwareCalleeAbiNextProofTarget'),
      calleeAbiNearPositiveStatus: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStatus',
      ),
      calleeAbiNearPositiveGap: detail(
        details,
        'valueAwareCalleeAbiNearPositiveGapCells',
      ),
      calleeAbiNearPositiveCurrentNet: detail(
        details,
        'valueAwareCalleeAbiNearPositiveCurrentNetCells',
      ),
      calleeAbiNearPositivePrimaryNet: detail(
        details,
        'valueAwareCalleeAbiNearPositivePrimaryNetCells',
      ),
      calleeAbiNearPositivePrimaryStatus: detail(
        details,
        'valueAwareCalleeAbiNearPositivePrimaryStatus',
      ),
      calleeAbiNearPositiveReason: detail(
        details,
        'valueAwareCalleeAbiNearPositiveReason',
      ),
      calleeAbiNearPositiveAction: detail(
        details,
        'valueAwareCalleeAbiNearPositiveAction',
      ),
      calleeAbiNearPositiveProofStatus: detail(
        details,
        'valueAwareCalleeAbiNearPositiveProofStatus',
      ),
      calleeAbiNearPositiveStackPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStackPlacementStatus',
      ),
      calleeAbiNearPositiveStackPlacementBasis: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStackPlacementBasis',
      ),
      calleeAbiNearPositiveStackPlacementRequiredAction: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStackPlacementRequiredAction',
      ),
      calleeAbiNearPositiveNaturalSurvivorLimit: detail(
        details,
        'valueAwareCalleeAbiNearPositiveNaturalSurvivorLimit',
      ),
      calleeAbiNearPositiveCurrentCost: detail(
        details,
        'valueAwareCalleeAbiNearPositiveCurrentCostBreakdown',
      ),
      calleeAbiNearPositivePrimaryCost: detail(
        details,
        'valueAwareCalleeAbiNearPositivePrimaryCostBreakdown',
      ),
      calleeAbiBestSubset: detail(details, 'valueAwareCalleeAbiBestSubsetInputNames'),
      calleeAbiSubsetCandidates: detail(details, 'valueAwareCalleeAbiSubsetCandidates'),
      calleeAbiBestSubsetNet: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNetAfterLowerBoundCells',
      ),
      calleeAbiBestSubsetNeed: detail(
        details,
        'valueAwareCalleeAbiBestSubsetAdditionalNetCellsToPositive',
      ),
      calleeAbiBestSubsetCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiBestSubsetCostBreakdown',
      ),
      calleeAbiBestSubsetLevers: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPositiveLevers',
      ),
      calleeAbiBestSubsetGapReason: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPositiveGapReason',
      ),
      calleeAbiBestSubsetNextProofTarget: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNextProofTarget',
      ),
      calleeAbiBestSubsetStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPlanStatus',
      ),
      calleeAbiBestSubsetNearPositiveStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveStatus',
      ),
      calleeAbiBestSubsetNearPositiveGap: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveGapCells',
      ),
      calleeAbiBestSubsetNearPositiveCurrentNet: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveCurrentNetCells',
      ),
      calleeAbiBestSubsetNearPositivePrimaryNet: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositivePrimaryNetCells',
      ),
      calleeAbiBestSubsetNearPositivePrimaryStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositivePrimaryStatus',
      ),
      calleeAbiBestSubsetNearPositiveReason: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveReason',
      ),
      calleeAbiBestSubsetNearPositiveAction: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveAction',
      ),
      calleeAbiBestSubsetNearPositiveProofStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveProofStatus',
      ),
      calleeAbiBestSubsetNearPositiveStackPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveStackPlacementStatus',
      ),
      calleeAbiBestSubsetNearPositiveStackPlacementBasis: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveStackPlacementBasis',
      ),
      calleeAbiBestSubsetNearPositiveStackPlacementRequiredAction: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveStackPlacementRequiredAction',
      ),
      calleeAbiBestSubsetNearPositiveNaturalSurvivorLimit: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveNaturalSurvivorLimit',
      ),
      calleeAbiBestSubsetNearPositiveCurrentCost: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositiveCurrentCostBreakdown',
      ),
      calleeAbiBestSubsetNearPositivePrimaryCost: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNearPositivePrimaryCostBreakdown',
      ),
      calleeAbiPrimaryEntryCoverage: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryCoverageByCallee',
      ),
      calleeAbiPrimaryEntryEligibleTargets: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryEligibleTargets',
      ),
      calleeAbiPrimaryEntryBlockedTargets: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryBlockedTargets',
      ),
      calleeAbiPrimaryEntryProtocol: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProtocolByCallee',
      ),
      calleeAbiPrimaryEntryProtocolStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProtocolStatus',
      ),
      calleeAbiPrimaryEntryProtocolAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProtocolLoweringAction',
      ),
      calleeAbiPrimaryEntryArgumentRestage: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryArgumentRestageByCallee',
      ),
      calleeAbiPrimaryEntryArgumentRestageSites: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryArgumentRestageSitesByCallee',
      ),
      calleeAbiPrimaryEntryPlacementSiteModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementSiteModelByCallee',
      ),
      calleeAbiPrimaryEntryPlacementSiteModelStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementSiteModelStatus',
      ),
      calleeAbiPrimaryEntryPlacementSiteModelAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementSiteModelAction',
      ),
      calleeAbiPrimaryEntryPreCallPlacement: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementByCallee',
      ),
      calleeAbiPrimaryEntryPreCallPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementStatus',
      ),
      calleeAbiPrimaryEntryPreCallPlacementAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementAction',
      ),
      calleeAbiPrimaryEntryPreCallPlacementProof: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofByCallee',
      ),
      calleeAbiPrimaryEntryPreCallPlacementProofStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofStatus',
      ),
      calleeAbiPrimaryEntryPreCallPlacementProofAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofAction',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteEstimate: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteEstimateByCallee',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteStatus',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteModel',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteAction',
      ),
      calleeAbiPrimaryEntrySlotSearch: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchByCallee',
      ),
      calleeAbiPrimaryEntrySlotSearchStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchStatus',
      ),
      calleeAbiPrimaryEntrySlotSearchAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchAction',
      ),
      calleeAbiPrimaryEntrySlotSearchActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeCandidate: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeCandidateByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapePreCallRewriteCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapePreCallRewriteCells',
      ),
      calleeAbiPrimaryEntrySlotShapeCalleeLowerBoundCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeCalleeLowerBoundCells',
      ),
      calleeAbiPrimaryEntrySlotShapeModeledPlacementCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeModeledPlacementCells',
      ),
      calleeAbiPrimaryEntrySlotShapeBasis: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBasis',
      ),
      calleeAbiPrimaryEntrySlotShapeStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeAction',
      ),
      calleeAbiPrimaryEntrySlotShapeActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeSafeFallback: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeSafeFallbackStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeSafeFallbackAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackAction',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirement: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirementAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementAction',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirementActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocation: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationModel',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationAction',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopy: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyAction',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyActionByCallee',
      ),
      calleeAbiPrimaryEntryPlacementLowerBound: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementLowerBoundCells',
      ),
      calleeAbiPrimaryEntryPlacementLowerBoundByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementLowerBoundByCallee',
      ),
      calleeAbiPrimaryEntryPlacementLowerBoundBasis: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementLowerBoundBasis',
      ),
      calleeAbiPrimaryEntryModeledPlacement: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementByCallee',
      ),
      calleeAbiPrimaryEntryModeledPlacementCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementCells',
      ),
      calleeAbiPrimaryEntryModeledPlacementBasis: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementBasis',
      ),
      calleeAbiPrimaryEntryCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryCostBreakdown',
      ),
      calleeAbiPrimaryEntryPlacementCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementCostBreakdown',
      ),
      calleeAbiPrimaryEntryModeledPlacementCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementCostBreakdown',
      ),
      calleeAbiPrimaryEntrySlotShapeCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeCostBreakdown',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown',
      ),
      calleeAbiPrimaryEntryNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterLowerBoundCells',
      ),
      calleeAbiPrimaryEntryPlacementNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterPlacementLowerBoundCells',
      ),
      calleeAbiPrimaryEntryModeledPlacementNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterModeledPlacementCells',
      ),
      calleeAbiPrimaryEntrySlotShapeNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeCells',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeExplicitTempCopyCells',
      ),
      calleeAbiPrimaryEntryNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntryPlacementNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntryModeledPlacementNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntrySlotShapeNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntryStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryStatus',
      ),
      calleeAbiPrimaryEntryPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementStatus',
      ),
      calleeAbiPrimaryEntryModeledPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeModelStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeModelStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus',
      ),
      calleeAbiPrimaryEntryModeledPlacementRequiredAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementRequiredAction',
      ),
      calleeAbiPrimaryEntrySlotShapeRequiredAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRequiredAction',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction',
      ),
      calleeAbiBestSubsetPrimaryEntryCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryCostBreakdown',
      ),
      calleeAbiBestSubsetPrimaryEntryPlacementCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryPlacementCostBreakdown',
      ),
      calleeAbiBestSubsetPrimaryEntryNet: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryNetAfterLowerBoundCells',
      ),
      calleeAbiBestSubsetPrimaryEntryPlacementLowerBound: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryPlacementLowerBoundCells',
      ),
      calleeAbiBestSubsetPrimaryEntryPlacementNet: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryNetAfterPlacementLowerBoundCells',
      ),
      calleeAbiBestSubsetPrimaryEntryNeed: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryAdditionalNetCellsToPositive',
      ),
      calleeAbiBestSubsetPrimaryEntryPlacementNeed: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryPlacementAdditionalNetCellsToPositive',
      ),
      calleeAbiBestSubsetPrimaryEntryStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryStatus',
      ),
      calleeAbiBestSubsetPrimaryEntryPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPrimaryEntryPlacementStatus',
      ),
      calleeAbiStatus:
        detail(details, 'valueAwareCalleeAbiCostModelStatus') ||
        detail(details, 'valueAwareCallPreservationCalleeStatus'),
    });
  }
  const nextActions = (report.nextActions ?? []).map((action) => {
    const details = action.bestDetails ?? {};
    return {
      file,
      source: action.source,
      action: action.action,
      status: action.status || 'positive',
      opportunities: action.opportunities,
      potentialSavings: action.potentialSavings,
      bestSavings: action.bestSavings,
      bestVariant: action.bestVariant ?? '',
      bestBlockerKind: action.bestBlockerKind ?? '',
      helper: action.bestDetails?.helperLabel ?? '',
      profitableInputs: detail(details, 'valueAwareProfitableStackInputNames'),
      breakEvenInputs: detail(details, 'valueAwareBreakEvenStackInputNames'),
      breakEvenInputCount: detail(details, 'valueAwareBreakEvenStackInputCount'),
      breakEvenInputGross: detail(details, 'valueAwareBreakEvenStackInputGrossCells'),
      breakEvenInputMaterialize: detail(
        details,
        'valueAwareBreakEvenStackInputMaterializeCells',
      ),
      breakEvenInputNet: detail(details, 'valueAwareBreakEvenStackInputNetCells'),
      breakEvenInputStatus: detail(details, 'valueAwareBreakEvenStackInputPlanStatus'),
      breakEvenInputNeed: detail(
        details,
        'valueAwareBreakEvenStackInputAdditionalNetCellsToPositive',
      ),
      breakEvenInputGapReason: detail(
        details,
        'valueAwareBreakEvenStackInputPositiveGapReason',
      ),
      breakEvenInputNextProofTarget: detail(
        details,
        'valueAwareBreakEvenStackInputNextProofTarget',
      ),
      breakEvenInputRequiredAction: detail(
        details,
        'valueAwareBreakEvenStackInputRequiredAction',
      ),
      unprofitableInputs: detail(details, 'valueAwareUnprofitableStackInputNames'),
      bestInput: detail(details, 'valueAwareBestStackInputCandidate'),
      bestInputNet: detail(details, 'valueAwareBestStackInputNetCells'),
      bestInputAdditionalRecallCellsToProfit:
        detail(details, 'valueAwareBestStackInputAdditionalRecallCellsToProfit'),
      bestInputGapReason: detail(details, 'valueAwareBestStackInputPositiveGapReason'),
      bestInputNextProofTarget: detail(details, 'valueAwareBestStackInputNextProofTarget'),
      profitBreakdown: detail(details, 'valueAwareStackInputProfitBreakdown'),
      materializeCellsByName: detail(details, 'valueAwareStackInputMaterializeCellsByName'),
      proofEffortPriority: detail(details, 'proofEffortPriority'),
      proofEffortReason: detail(details, 'proofEffortReason'),
      proofEffortSizeGate: detail(details, 'proofEffortSizeGate'),
      sizeFirstAction: detail(details, 'sizeFirstAction'),
      layoutProofScope: detail(details, 'layoutProofScope'),
      layoutProofStatus: detail(details, 'layoutProofStatus'),
      layoutProofRequirement: detail(details, 'layoutProofRequirement'),
      layoutProofRequiredArtifact: detail(
        details,
        'layoutProofRequiredArtifact',
      ),
      layoutProofConflictModel: detail(details, 'layoutProofConflictModel'),
      layoutProofNextAction: detail(details, 'layoutProofNextAction'),
      callArgumentPreservationCellsByCallee: detail(
        details,
        'valueAwareCallArgumentPreservationCellsByCallee',
      ),
      callArgumentPreservationRaw: detail(
        details,
        'valueAwareCallArgumentPreservationRawCells',
      ),
      callArgumentPreservationZeroCopy: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopyCells',
      ),
      callArgumentPreservationZeroCopyStatus: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopyStatus',
      ),
      callArgumentPreservationZeroCopySites: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopySites',
      ),
      callArgumentPreservationZeroCopyBlockers: detail(
        details,
        'valueAwareCallArgumentPreservationZeroCopyBlockers',
      ),
      callArgumentPreservationLowerBound: detail(
        details,
        'valueAwareCallArgumentPreservationLowerBoundCells',
      ),
      callArgumentPreservationLowerBoundBasis: detail(
        details,
        'valueAwareCallArgumentPreservationLowerBoundBasis',
      ),
      callArgumentPreservationRequiredAction: detail(
        details,
        'valueAwareCallArgumentPreservationRequiredAction',
      ),
      callArgumentX2RestoreStatus: detail(details, 'valueAwareCallArgumentX2RestoreStatus'),
      callArgumentX2MutationOpcodesByCallee: detail(
        details,
        'valueAwareCallArgumentX2MutationOpcodesByCallee',
      ),
      callArgumentX2ClobberClassesByCallee: detail(
        details,
        'valueAwareCallArgumentX2ClobberClassesByCallee',
      ),
      callArgumentX2PreloadConstantOpcodesByCallee: detail(
        details,
        'valueAwareCallArgumentX2PreloadConstantOpcodesByCallee',
      ),
      callArgumentX2PreloadLiteralReplacementByCallee: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementByCallee',
      ),
      callArgumentX2PreloadLiteralReplacementStatus: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementStatus',
      ),
      callArgumentX2PreloadLiteralReplacementDelta: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementDeltaCells',
      ),
      callArgumentX2PreloadLiteralReplacementNet: detail(
        details,
        'valueAwareCallArgumentX2PreloadLiteralReplacementHypotheticalNetCells',
      ),
      callArgumentX2PreloadRefactorRequiredAction: detail(
        details,
        'valueAwareCallArgumentX2PreloadRefactorRequiredAction',
      ),
      callArgumentX2RequiredAction: detail(
        details,
        'valueAwareCallArgumentX2RequiredAction',
      ),
      callArgumentSites: detail(details, 'valueAwareCallArgumentSites'),
      callArgumentInputNamesByCallee: detail(
        details,
        'valueAwareCallArgumentInputNamesByCallee',
      ),
      callPreservationSites: detail(details, 'valueAwareCallPreservationSites'),
      symbolicEntryStackByCallSite: detail(
        details,
        'valueAwareSymbolicEntryStackByCallSite',
      ),
      symbolicEntryStackSeed: detail(details, 'valueAwareSymbolicEntryStackSeed'),
      symbolicEntryStackSeedStatus: detail(
        details,
        'valueAwareSymbolicEntryStackSeedStatus',
      ),
      symbolicKnownCalleeStackEffects: detail(
        details,
        'valueAwareSymbolicKnownCalleeStackEffects',
      ),
      symbolicFlowEntryStack: detail(details, 'valueAwareSymbolicFlowEntryStack'),
      entryStackLostKnownFacts: detail(
        details,
        'valueAwareEntryStackLostKnownFacts',
      ),
      entryStackLostKnownFactCount: detail(
        details,
        'valueAwareEntryStackLostKnownFactCount',
      ),
      entryStackLostKnownFactSlots: detail(
        details,
        'valueAwareEntryStackLostKnownFactSlots',
      ),
      entryStackLostKnownFactStatus: detail(
        details,
        'valueAwareEntryStackLostKnownFactStatus',
      ),
      entryStackLostKnownFactTarget: detail(
        details,
        'valueAwareEntryStackLostKnownFactNextProofTarget',
      ),
      entryStackLostKnownFactAction: detail(
        details,
        'valueAwareEntryStackLostKnownFactRequiredAction',
      ),
      existingEntryStackInputSites: detail(
        details,
        'valueAwareExistingEntryStackInputSites',
      ),
      existingEntryStackInputSitesByName: detail(
        details,
        'valueAwareExistingEntryStackInputSitesByName',
      ),
      selectedStackCarriedPlan: detail(
        details,
        'valueAwareSelectedStackCarriedPlan',
      ),
      selectedStackCarriedStatus: detail(
        details,
        'valueAwareSelectedStackCarriedStatus',
      ),
      selectedStackCarriedInputs: detail(
        details,
        'valueAwareSelectedStackCarriedInputNames',
      ),
      selectedStackCarriedTargets: detail(
        details,
        'valueAwareSelectedStackCarriedTargets',
      ),
      selectedStackCarriedSites: detail(
        details,
        'valueAwareSelectedStackCarriedSites',
      ),
      calleeNaturalFirstRecallChoice: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceByCallee',
      ),
      calleeNaturalFirstRecallChoiceStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceStatusByCallee',
      ),
      calleeNaturalPreservedSlotFinalSlots: detail(
        details,
        'valueAwareCalleeAbiNaturalPreservedSlotFinalSlotsByCallee',
      ),
      calleeNaturalFirstRecallUseShape: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallUseShapeByCallee',
      ),
      calleeNaturalFirstRecallUseShapeStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallUseShapeStatus',
      ),
      calleeNaturalFirstRecallUseShapeAction: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallUseShapeAction',
      ),
      calleeNaturalFirstRecallChoiceSearch: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceSearchByCallee',
      ),
      calleeNaturalFirstRecallChoiceSearchStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceSearchStatusByCallee',
      ),
      calleeAbiCostBreakdown: detail(details, 'valueAwareCalleeAbiCostBreakdown'),
      calleeAbiNetAfterLowerBound: detail(
        details,
        'valueAwareCalleeAbiNetAfterLowerBoundCells',
      ),
      calleeAbiAdditionalNetToPositive: detail(
        details,
        'valueAwareCalleeAbiAdditionalNetCellsToPositive',
      ),
      calleeAbiPositiveLevers: detail(details, 'valueAwareCalleeAbiPositiveLevers'),
      calleeAbiPositiveGapReason: detail(
        details,
        'valueAwareCalleeAbiPositiveGapReason',
      ),
      calleeAbiNextProofTarget: detail(details, 'valueAwareCalleeAbiNextProofTarget'),
      calleeAbiNearPositivePrimaryNet: detail(
        details,
        'valueAwareCalleeAbiNearPositivePrimaryNetCells',
      ),
      calleeAbiNearPositivePrimaryStatus: detail(
        details,
        'valueAwareCalleeAbiNearPositivePrimaryStatus',
      ),
      calleeAbiNearPositiveStackPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStackPlacementStatus',
      ),
      calleeAbiNearPositiveStackPlacementBasis: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStackPlacementBasis',
      ),
      calleeAbiNearPositiveStackPlacementRequiredAction: detail(
        details,
        'valueAwareCalleeAbiNearPositiveStackPlacementRequiredAction',
      ),
      calleeAbiBestSubset: detail(details, 'valueAwareCalleeAbiBestSubsetInputNames'),
      calleeAbiSubsetCandidates: detail(details, 'valueAwareCalleeAbiSubsetCandidates'),
      calleeAbiBestSubsetNet: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNetAfterLowerBoundCells',
      ),
      calleeAbiBestSubsetNeed: detail(
        details,
        'valueAwareCalleeAbiBestSubsetAdditionalNetCellsToPositive',
      ),
      calleeAbiBestSubsetCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiBestSubsetCostBreakdown',
      ),
      calleeAbiBestSubsetLevers: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPositiveLevers',
      ),
      calleeAbiBestSubsetGapReason: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPositiveGapReason',
      ),
      calleeAbiBestSubsetNextProofTarget: detail(
        details,
        'valueAwareCalleeAbiBestSubsetNextProofTarget',
      ),
      calleeAbiBestSubsetStatus: detail(
        details,
        'valueAwareCalleeAbiBestSubsetPlanStatus',
      ),
      calleeAbiPureStackPlacement: detail(
        details,
        'valueAwareCalleeAbiPureStackPlacementByCallee',
      ),
      calleeAbiPureStackPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPureStackPlacementStatus',
      ),
      calleeAbiPrimaryEntryProofDisposition: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProofDisposition',
      ),
      calleeAbiPrimaryEntryProtocol: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProtocolByCallee',
      ),
      calleeAbiPrimaryEntryProtocolStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProtocolStatus',
      ),
      calleeAbiPrimaryEntryProtocolAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryProtocolLoweringAction',
      ),
      calleeAbiPrimaryEntryArgumentRestage: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryArgumentRestageByCallee',
      ),
      calleeAbiPrimaryEntryArgumentRestageSites: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryArgumentRestageSitesByCallee',
      ),
      calleeAbiPrimaryEntryPlacementSiteModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementSiteModelByCallee',
      ),
      calleeAbiPrimaryEntryPlacementSiteModelStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementSiteModelStatus',
      ),
      calleeAbiPrimaryEntryPlacementSiteModelAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementSiteModelAction',
      ),
      calleeAbiPrimaryEntryPreCallPlacement: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementByCallee',
      ),
      calleeAbiPrimaryEntryPreCallPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementStatus',
      ),
      calleeAbiPrimaryEntryPreCallPlacementAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementAction',
      ),
      calleeAbiPrimaryEntryPreCallPlacementProof: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofByCallee',
      ),
      calleeAbiPrimaryEntryPreCallPlacementProofStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofStatus',
      ),
      calleeAbiPrimaryEntryPreCallPlacementProofAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementProofAction',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteEstimate: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteEstimateByCallee',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteStatus',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteModel',
      ),
      calleeAbiPrimaryEntryPreCallPlacementRewriteAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPreCallPlacementRewriteAction',
      ),
      calleeAbiPrimaryEntrySlotSearch: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchByCallee',
      ),
      calleeAbiPrimaryEntrySlotSearchStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchStatus',
      ),
      calleeAbiPrimaryEntrySlotSearchAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchAction',
      ),
      calleeAbiPrimaryEntrySlotSearchActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotSearchActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeCandidate: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeCandidateByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapePreCallRewriteCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapePreCallRewriteCells',
      ),
      calleeAbiPrimaryEntrySlotShapeCalleeLowerBoundCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeCalleeLowerBoundCells',
      ),
      calleeAbiPrimaryEntrySlotShapeModeledPlacementCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeModeledPlacementCells',
      ),
      calleeAbiPrimaryEntrySlotShapeBasis: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBasis',
      ),
      calleeAbiPrimaryEntrySlotShapeStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeAction',
      ),
      calleeAbiPrimaryEntrySlotShapeActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeSafeFallback: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeSafeFallbackStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeSafeFallbackAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeSafeFallbackAction',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirement: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirementAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementAction',
      ),
      calleeAbiPrimaryEntrySlotShapeRoleRequirementActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRoleRequirementActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocation: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationModel',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationAction',
      ),
      calleeAbiPrimaryEntrySlotShapeBodyRelocationActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeBodyRelocationActionByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopy: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyByCallee',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyAction',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyActionByCallee: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyActionByCallee',
      ),
      calleeAbiPrimaryEntryModeledPlacement: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementByCallee',
      ),
      calleeAbiPrimaryEntryModeledPlacementCells: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementCells',
      ),
      calleeAbiPrimaryEntryModeledPlacementBasis: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementBasis',
      ),
      calleeAbiPrimaryEntryModeledPlacementCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementCostBreakdown',
      ),
      calleeAbiPrimaryEntrySlotShapeCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeCostBreakdown',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown',
      ),
      calleeAbiPrimaryEntryModeledPlacementNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterModeledPlacementCells',
      ),
      calleeAbiPrimaryEntrySlotShapeNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeCells',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterSlotShapeExplicitTempCopyCells',
      ),
      calleeAbiPrimaryEntryModeledPlacementNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntrySlotShapeNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntryModeledPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeModelStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeModelStatus',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus',
      ),
      calleeAbiPrimaryEntryModeledPlacementRequiredAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryModeledPlacementRequiredAction',
      ),
      calleeAbiPrimaryEntrySlotShapeRequiredAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeRequiredAction',
      ),
      calleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction',
      ),
    };
  });
  return {
    file,
    totalCells: report.totalCells,
    helpers,
    candidateOpportunities,
    helperTraffic,
    nextActions,
  };
}

function aggregate(rows) {
  const nextActionGroups = new Map();
  const helperPlanGroups = new Map();
  const candidateBlockerGroups = new Map();
  const seenActionGroupFiles = new Set();
  const seenHelperGroupFiles = new Set();
  const seenCandidateGroupFiles = new Set();
  for (const row of rows) {
    for (const action of row.nextActions) {
      const key = `${action.status}\t${action.source}\t${action.action}`;
      const fileKey = `${key}\t${row.file}`;
      addGrouped(nextActionGroups, key, {
        ...action,
        firstInGroup: !seenActionGroupFiles.has(fileKey),
      });
      seenActionGroupFiles.add(fileKey);
    }
    for (const helper of row.helperTraffic) {
      const key = `${helper.planStatus || 'unknown'}\t${helper.requiredAction || 'none'}\t${helper.costModelAction || 'none'}`;
      const fileKey = `${key}\t${row.file}`;
      addGrouped(helperPlanGroups, key, {
        ...helper,
        firstInGroup: !seenHelperGroupFiles.has(fileKey),
      });
      seenHelperGroupFiles.add(fileKey);
    }
    for (const candidate of row.candidateOpportunities) {
      const key = `${candidate.blockerKind || candidate.variant || 'unknown'}\t${candidate.requiredAction || 'none'}\t${candidate.proofDisposition || candidate.proofStatus || 'none'}`;
      const fileKey = `${key}\t${row.file}`;
      addGrouped(candidateBlockerGroups, key, {
        ...candidate,
        firstInGroup: !seenCandidateGroupFiles.has(fileKey),
      });
      seenCandidateGroupFiles.add(fileKey);
    }
  }
  const sortGroups = (groups) => [...groups.values()].sort((left, right) => {
    if (right.potentialSavings !== left.potentialSavings) {
      return right.potentialSavings - left.potentialSavings;
    }
    if (right.bestSavings !== left.bestSavings) {
      return right.bestSavings - left.bestSavings;
    }
    return left.key.localeCompare(right.key);
  });
  return {
    analyzedFiles: rows.length,
    totalCells: rows.reduce((sum, row) => sum + row.totalCells, 0),
    helperCostCount: rows.reduce((sum, row) => sum + row.helpers.length, 0),
    topHelpers: rows.flatMap((row) => row.helpers)
      .filter((row) => row.totalCells > 0)
      .sort((left, right) =>
        right.totalCells - left.totalCells ||
        right.callSiteCells - left.callSiteCells ||
        left.file.localeCompare(right.file) ||
        left.label.localeCompare(right.label)),
    helperTrafficCount: rows.reduce((sum, row) => sum + row.helperTraffic.length, 0),
    candidateOpportunityCount:
      rows.reduce((sum, row) => sum + row.candidateOpportunities.length, 0),
    positiveCandidateOpportunities: rows.flatMap((row) => row.candidateOpportunities)
      .filter((row) => row.savings > 0)
      .sort((left, right) =>
        right.savings - left.savings ||
        left.file.localeCompare(right.file) ||
        left.variant.localeCompare(right.variant)),
    topRejectedCandidateOpportunities: rows.flatMap((row) => row.candidateOpportunities)
      .filter((row) => row.savings <= 0)
      .sort((left, right) =>
        right.savings - left.savings ||
        left.file.localeCompare(right.file) ||
        left.variant.localeCompare(right.variant)),
    positiveHelperTraffic: rows.flatMap((row) => row.helperTraffic)
      .filter((row) => row.savings > 0)
      .sort((left, right) => right.savings - left.savings || left.file.localeCompare(right.file)),
    stalledHelperTraffic: rows.flatMap((row) => row.helperTraffic)
      .filter((row) => row.savings <= 0)
      .sort((left, right) => right.savings - left.savings || left.file.localeCompare(right.file)),
    nearPositiveHelperTraffic: rows.flatMap((row) => row.helperTraffic)
      .filter((row) => row.savings <= 0)
      .map((row) => ({ ...row, positiveGap: helperPositiveGap(row) }))
      .filter((row) => row.positiveGap !== null && row.positiveGap <= 1)
      .sort((left, right) =>
        left.positiveGap - right.positiveGap ||
        right.registerTrafficCells - left.registerTrafficCells ||
        left.file.localeCompare(right.file)),
    positiveNextActions: rows.flatMap((row) => row.nextActions)
      .filter((row) => row.status === 'positive')
      .sort((left, right) =>
        right.potentialSavings - left.potentialSavings || left.file.localeCompare(right.file)),
    stalledNextActions: rows.flatMap((row) => row.nextActions)
      .filter((row) => row.status !== 'positive')
      .sort((left, right) =>
        right.bestSavings - left.bestSavings || left.file.localeCompare(right.file)),
    nextActionGroups: sortGroups(nextActionGroups),
    helperPlanGroups: sortGroups(helperPlanGroups),
    candidateBlockerGroups: sortGroups(candidateBlockerGroups),
    files: rows,
  };
}

function printGroup(group) {
  const best = group.best ?? {};
  console.log(
    `- ${group.key.replaceAll('\t', ' :: ')} files=${group.files}` +
      ` opportunities=${group.opportunities}` +
      ` potential=${group.potentialSavings}` +
      ` best=${group.bestSavings}` +
      ` bestFile=${best.file ?? ''}` +
      ` helper=${best.helper ?? ''}` +
      ` variant=${best.bestVariant ?? best.variant ?? ''}` +
      candidateGroupDetailSuffix(best),
  );
}

function candidateGroupDetailSuffix(row) {
  if (!row || (!row.deadIntegerProofGap && !row.consumerControlKind &&
      !row.proofEffortPriority && !row.layoutProofStatus && !row.breakEvenInputs)) {
    return '';
  }
  const fields = [
    ['proofPriority', row.proofEffortPriority],
    ['proofReason', row.proofEffortReason],
    ['sizeFirst', row.sizeFirstAction],
    ['layoutScope', row.layoutProofScope],
    ['layoutStatus', row.layoutProofStatus],
    ['layoutReq', row.layoutProofRequirement],
    ['layoutArtifact', row.layoutProofRequiredArtifact],
    ['layoutConflict', row.layoutProofConflictModel],
    ['layoutNext', row.layoutProofNextAction],
    ['breakEven', row.breakEvenInputs],
    ['breakEvenNet', row.breakEvenInputNet],
    ['breakEvenStatus', row.breakEvenInputStatus],
    ['breakEvenNeed', row.breakEvenInputNeed],
    ['breakEvenAction', row.breakEvenInputRequiredAction],
    ['bestInput', row.bestInput],
    ['bestNet', row.bestInputNet],
    ['bestTarget', row.bestInputNextProofTarget],
    ['consumer', row.fractionalSelectorConsumer],
    ['consumerAddr', row.consumerAddress],
    ['consumerKind', row.consumerControlKind],
    ['consumerReg', row.deadIntegerConsumerRegister],
    ['carrierReg', row.deadIntegerSelectorCarrierRegister],
    ['sourceReg', row.fractionalSelectorSourceRegister],
    ['selectorTarget', row.selectorTarget],
    ['intRole', row.integerPartUseRole],
    ['fracRole', row.fractionalPartUseRole],
    ['intSafety', row.integerPartDataSafetyStatus],
    ['proofGap', row.deadIntegerProofGap],
    ['artifact', row.deadIntegerProofRequiredArtifact],
    ['next', row.deadIntegerProofNextAction],
  ];
  const rendered = fields
    .filter(([, value]) => value !== undefined && value !== null && value !== '')
    .map(([name, value]) => `${name}=${value}`);
  return rendered.length === 0 ? '' : ` ${rendered.join(' ')}`;
}

function printHelperCost(row) {
  console.log(
    `- ${row.totalCells} ${row.file} :: ${row.label}` +
      ` body=${row.bodyCells}` +
      ` calls=${row.callSiteCells}` +
      ` callOccurrences=${row.callOccurrences}` +
      ` bodyOccurrences=${row.bodyOccurrences}` +
      ` addr=${row.firstAddress}-${row.lastAddress}` +
      ` role=${row.role || '-'}` +
      ` shape=${row.pipelineShape || '-'}` +
      ` bodyPerTerm=${row.bodyCellsPerAccumulatorTerm || '-'}` +
      ` callPerOccurrence=${row.callCellsPerOccurrence || '-'}` +
      ` statePolicy=${row.accumulatorStatePolicy || '-'}` +
      ` interleaved=${row.interleavedPipelineShape || '-'}` +
      ` blocker=${row.interleavedPipelineBlocker || '-'}` +
      ` next=${row.nextPipelineAction || '-'}` +
      ` proof=${row.nextPipelineProofTarget || '-'}` +
      ` argRecalls=${row.valueAwareArgumentRecallSites || '-'}` +
      ` repeatedArgRecalls=${row.valueAwareRepeatedArgumentRecallSites || '-'}` +
      ` topRepeatedArg=${row.valueAwareTopRepeatedArgumentRecall || '-'}` +
      ` schedulerTarget=${row.valueAwareSchedulerNextMaterializationTarget || '-'}` +
      ` schedulerFeasibility=${row.valueAwareRepeatedArgumentSchedulerFeasibility || '-'}` +
      ` schedulerStatus=${row.valueAwareRepeatedArgumentSchedulerStatus || '-'}` +
      ` schedulerAction=${row.valueAwareRepeatedArgumentSchedulerAction || '-'}` +
      ` selectedStack=${row.selectedStackCarriedInputs || '-'}` +
      ` selectedStackTargets=${row.selectedStackCarriedTargets || '-'}` +
      ` knownCallee=${row.symbolicKnownCalleeStackEffects || '-'}`,
  );
}

function printCandidateOpportunity(row) {
  console.log(
    `- ${row.savings} ${row.file} :: ${row.variant}` +
      ` site=${row.site || '-'}` +
      ` steps=${row.currentSteps}->${row.candidateSteps}` +
      ` blocker=${row.blockerKind || '-'}` +
      ` required=${row.requiredAction || '-'}` +
      ` proof=${row.proofStatus || '-'}` +
      ` disposition=${row.proofDisposition || '-'}` +
      ` size=${row.sizeImpactStatus || '-'}` +
      ` net=${row.netSavingsStatus || '-'}` +
      ` candidate=${row.candidateStepsStatus || '-'}` +
      ` estimate=${row.estimateKind || '-'}` +
      ` proofPriority=${row.proofEffortPriority || '-'}` +
      ` proofReason=${row.proofEffortReason || '-'}` +
      ` proofSizeGate=${row.proofEffortSizeGate || '-'}` +
      ` sizeFirst=${row.sizeFirstAction || '-'}` +
      ` layoutScope=${row.layoutProofScope || '-'}` +
      ` layoutStatus=${row.layoutProofStatus || '-'}` +
      ` layoutReq=${row.layoutProofRequirement || '-'}` +
      ` layoutArtifact=${row.layoutProofRequiredArtifact || '-'}` +
      ` layoutConflict=${row.layoutProofConflictModel || '-'}` +
      ` layoutNext=${row.layoutProofNextAction || '-'}` +
      ` consumer=${row.fractionalSelectorConsumer || '-'}` +
      ` consumerAddr=${row.consumerAddress || '-'}` +
      ` consumerKind=${row.consumerControlKind || '-'}` +
      ` consumerReg=${row.deadIntegerConsumerRegister || '-'}` +
      ` carrierReg=${row.deadIntegerSelectorCarrierRegister || '-'}` +
      ` sourceReg=${row.fractionalSelectorSourceRegister || '-'}` +
      ` selectorTarget=${row.selectorTarget || '-'}` +
      ` intRole=${row.integerPartUseRole || '-'}` +
      ` fracRole=${row.fractionalPartUseRole || '-'}` +
      ` intSafety=${row.integerPartDataSafetyStatus || '-'}` +
      ` proofGap=${row.deadIntegerProofGap || '-'}` +
      ` proofArtifact=${row.deadIntegerProofRequiredArtifact || '-'}` +
      ` proofNext=${row.deadIntegerProofNextAction || '-'}` +
      ` stackEntryDelta=${row.stackResidentEntryDelta || '-'}` +
      ` stackEntryStatus=${row.stackResidentEntryMeasuredStatus || '-'}` +
      ` stackEntryAbi=${row.stackResidentEntryAbiContext || '-'}` +
      ` stackEntryBlockers=${row.stackResidentEntryValueAwareContext || '-'}` +
      ` stackEntryAction=${row.stackResidentEntryRequiredAction || '-'}` +
      ` reason=${row.reason || '-'}`,
  );
}

function actionDetailSuffix(row) {
  const fields = [
    ['argPreserve', row.callArgumentPreservationCellsByCallee],
    ['proofPriority', row.proofEffortPriority],
    ['proofReason', row.proofEffortReason],
    ['proofSizeGate', row.proofEffortSizeGate],
    ['sizeFirst', row.sizeFirstAction],
    ['layoutScope', row.layoutProofScope],
    ['layoutStatus', row.layoutProofStatus],
    ['layoutReq', row.layoutProofRequirement],
    ['layoutArtifact', row.layoutProofRequiredArtifact],
    ['layoutConflict', row.layoutProofConflictModel],
    ['layoutNext', row.layoutProofNextAction],
    ['profitableInputs', row.profitableInputs],
    ['breakEven', row.breakEvenInputs],
    ['breakEvenCount', row.breakEvenInputCount],
    ['breakEvenGross', row.breakEvenInputGross],
    ['breakEvenMaterialize', row.breakEvenInputMaterialize],
    ['breakEvenNet', row.breakEvenInputNet],
    ['breakEvenStatus', row.breakEvenInputStatus],
    ['breakEvenNeed', row.breakEvenInputNeed],
    ['breakEvenGap', row.breakEvenInputGapReason],
    ['breakEvenTarget', row.breakEvenInputNextProofTarget],
    ['breakEvenAction', row.breakEvenInputRequiredAction],
    ['unprofitableInputs', row.unprofitableInputs],
    ['bestInput', row.bestInput],
    ['bestNet', row.bestInputNet],
    ['bestNeed', row.bestInputAdditionalRecallCellsToProfit],
    ['bestGap', row.bestInputGapReason],
    ['bestTarget', row.bestInputNextProofTarget],
    ['profit', row.profitBreakdown],
    ['materialize', row.materializeCellsByName],
    ['argPreserveLower', row.callArgumentPreservationLowerBound],
    ['argPreserveBasis', row.callArgumentPreservationLowerBoundBasis],
    ['argPreserveAction', row.callArgumentPreservationRequiredAction],
    ['argX2', row.callArgumentX2RestoreStatus],
    ['argX2Action', row.callArgumentX2RequiredAction],
    ['x2Mutating', row.callArgumentX2MutationOpcodesByCallee],
    ['x2Class', row.callArgumentX2ClobberClassesByCallee],
    ['x2Preload', row.callArgumentX2PreloadConstantOpcodesByCallee],
    ['x2PreloadLiteral', row.callArgumentX2PreloadLiteralReplacementByCallee],
    ['x2PreloadLiteralStatus', row.callArgumentX2PreloadLiteralReplacementStatus],
    ['x2PreloadLiteralDelta', row.callArgumentX2PreloadLiteralReplacementDelta],
    ['x2PreloadLiteralNet', row.callArgumentX2PreloadLiteralReplacementNet],
    ['x2PreloadAction', row.callArgumentX2PreloadRefactorRequiredAction],
    ['argInputs', row.callArgumentInputNamesByCallee],
    ['argSites', row.callArgumentSites],
    ['argRecalls', row.valueAwareArgumentRecallSites],
    ['repeatedArgRecalls', row.valueAwareRepeatedArgumentRecallSites],
    ['topRepeatedArg', row.valueAwareTopRepeatedArgumentRecall],
    ['schedulerTarget', row.valueAwareSchedulerNextMaterializationTarget],
    ['schedulerFeasibility', row.valueAwareRepeatedArgumentSchedulerFeasibility],
    ['schedulerStatus', row.valueAwareRepeatedArgumentSchedulerStatus],
    ['schedulerAction', row.valueAwareRepeatedArgumentSchedulerAction],
    ['schedulerProfitableNet', row.valueAwareRepeatedArgumentSchedulerProfitableNet],
    ['schedulerModelNet', row.valueAwareRepeatedArgumentSchedulerModelNet],
    ['preserveSites', row.callPreservationSites],
    ['entryStack', row.symbolicEntryStackSeed],
    ['argPreserveRaw', row.callArgumentPreservationRaw],
    ['argZeroCopy', row.callArgumentPreservationZeroCopy],
    ['argZeroCopyStatus', row.callArgumentPreservationZeroCopyStatus],
    ['argZeroCopySites', row.callArgumentPreservationZeroCopySites],
    ['argZeroCopyBlockers', row.callArgumentPreservationZeroCopyBlockers],
    ['entryStackStatus', row.symbolicEntryStackSeedStatus],
    ['entryStackSites', row.symbolicEntryStackByCallSite],
    ['knownCallee', row.symbolicKnownCalleeStackEffects],
    ['flowEntry', row.symbolicFlowEntryStack],
    ['lostEntryFacts', row.entryStackLostKnownFacts],
    ['lostEntryFactCount', row.entryStackLostKnownFactCount],
    ['lostEntrySlots', row.entryStackLostKnownFactSlots],
    ['lostEntryStatus', row.entryStackLostKnownFactStatus],
    ['lostEntryTarget', row.entryStackLostKnownFactTarget],
    ['lostEntryAction', row.entryStackLostKnownFactAction],
    ['entryInputs', row.existingEntryStackInputSites],
    ['selectedStack', row.selectedStackCarriedInputs],
    ['selectedStackStatus', row.selectedStackCarriedStatus],
    ['selectedStackTargets', row.selectedStackCarriedTargets],
    ['selectedStackSites', row.selectedStackCarriedSites],
    ['abiCost', row.calleeAbiCostBreakdown],
    ['abiNet', row.calleeAbiNetAfterLowerBound],
    ['abiNeed', row.calleeAbiAdditionalNetToPositive],
    ['abiLevers', row.calleeAbiPositiveLevers],
    ['abiGap', row.calleeAbiPositiveGapReason],
    ['abiTarget', row.calleeAbiNextProofTarget],
    ['abiNearPrimaryNet', row.calleeAbiNearPositivePrimaryNet],
    ['abiNearPrimaryStatus', row.calleeAbiNearPositivePrimaryStatus],
    ['abiPlacement', row.calleeAbiNearPositiveStackPlacementStatus],
    ['abiPlacementBasis', row.calleeAbiNearPositiveStackPlacementBasis],
    ['abiPlacementAction', row.calleeAbiNearPositiveStackPlacementRequiredAction],
    ['abiPureStack', row.calleeAbiPureStackPlacement],
    ['abiProofDisposition', row.calleeAbiPrimaryEntryProofDisposition],
    ['abiProtocol', row.calleeAbiPrimaryEntryProtocol],
    ['abiProtocolStatus', row.calleeAbiPrimaryEntryProtocolStatus],
    ['abiProtocolAction', row.calleeAbiPrimaryEntryProtocolAction],
    ['abiArgRestage', row.calleeAbiPrimaryEntryArgumentRestage],
    ['abiArgRestageSites', row.calleeAbiPrimaryEntryArgumentRestageSites],
    ['abiSubsets', row.calleeAbiSubsetCandidates],
    ['abiSubset', row.calleeAbiBestSubset],
    ['abiSubsetNet', row.calleeAbiBestSubsetNet],
    ['abiSubsetNeed', row.calleeAbiBestSubsetNeed],
    ['abiSubsetCost', row.calleeAbiBestSubsetCostBreakdown],
    ['abiSubsetLevers', row.calleeAbiBestSubsetLevers],
    ['abiSubsetGap', row.calleeAbiBestSubsetGapReason],
    ['abiSubsetTarget', row.calleeAbiBestSubsetNextProofTarget],
    ['abiSubsetStatus', row.calleeAbiBestSubsetStatus],
    ['naturalChoice', row.calleeNaturalFirstRecallChoice],
    ['naturalChoiceStatus', row.calleeNaturalFirstRecallChoiceStatus],
    ['naturalFinalSlots', row.calleeNaturalPreservedSlotFinalSlots],
    ['naturalUseShape', row.calleeNaturalFirstRecallUseShape],
    ['naturalUseShapeStatus', row.calleeNaturalFirstRecallUseShapeStatus],
    ['naturalUseShapeAction', row.calleeNaturalFirstRecallUseShapeAction],
    ['naturalChoiceSearch', row.calleeNaturalFirstRecallChoiceSearch],
    ['naturalChoiceSearchStatus', row.calleeNaturalFirstRecallChoiceSearchStatus],
    ['abiPrimarySiteModel', row.calleeAbiPrimaryEntryPlacementSiteModel],
    ['abiPrimarySiteStatus', row.calleeAbiPrimaryEntryPlacementSiteModelStatus],
    ['abiPrimaryPreCallPlacement', row.calleeAbiPrimaryEntryPreCallPlacement],
    ['abiPrimaryPreCallPlacementStatus', row.calleeAbiPrimaryEntryPreCallPlacementStatus],
    ['abiPrimaryPreCallPlacementAction', row.calleeAbiPrimaryEntryPreCallPlacementAction],
    ['abiPrimaryPreCallProof', row.calleeAbiPrimaryEntryPreCallPlacementProof],
    ['abiPrimaryPreCallProofStatus', row.calleeAbiPrimaryEntryPreCallPlacementProofStatus],
    ['abiPrimaryPreCallProofAction', row.calleeAbiPrimaryEntryPreCallPlacementProofAction],
    ['abiPrimaryPreCallRewrite', row.calleeAbiPrimaryEntryPreCallPlacementRewriteEstimate],
    ['abiPrimaryPreCallRewriteStatus', row.calleeAbiPrimaryEntryPreCallPlacementRewriteStatus],
    ['abiPrimaryPreCallRewriteModel', row.calleeAbiPrimaryEntryPreCallPlacementRewriteModel],
    ['abiPrimaryPreCallRewriteAction', row.calleeAbiPrimaryEntryPreCallPlacementRewriteAction],
    ['abiPrimarySlotSearch', row.calleeAbiPrimaryEntrySlotSearch],
    ['abiPrimarySlotSearchStatus', row.calleeAbiPrimaryEntrySlotSearchStatus],
    ['abiPrimarySlotSearchAction', row.calleeAbiPrimaryEntrySlotSearchAction],
    ['abiPrimarySlotShape', row.calleeAbiPrimaryEntrySlotShapeCandidate],
    ['abiPrimarySlotShapeCost', row.calleeAbiPrimaryEntrySlotShapeCostBreakdown],
    ['abiPrimarySlotShapeNet', row.calleeAbiPrimaryEntrySlotShapeNet],
    ['abiPrimarySlotShapeStatus', row.calleeAbiPrimaryEntrySlotShapeModelStatus],
    ['abiPrimarySlotShapeAction', row.calleeAbiPrimaryEntrySlotShapeRequiredAction],
    ['abiPrimarySlotShapeSafeFallback', row.calleeAbiPrimaryEntrySlotShapeSafeFallback],
    ['abiPrimarySlotShapeSafeFallbackStatus',
      row.calleeAbiPrimaryEntrySlotShapeSafeFallbackStatus],
    ['abiPrimarySlotShapeSafeFallbackAction',
      row.calleeAbiPrimaryEntrySlotShapeSafeFallbackAction],
    ['abiPrimarySlotShapeRole', row.calleeAbiPrimaryEntrySlotShapeRoleRequirement],
    ['abiPrimarySlotShapeRoleStatus',
      row.calleeAbiPrimaryEntrySlotShapeRoleRequirementStatus],
    ['abiPrimarySlotShapeRoleAction',
      row.calleeAbiPrimaryEntrySlotShapeRoleRequirementAction],
    ['abiPrimarySlotShapeBody', row.calleeAbiPrimaryEntrySlotShapeBodyRelocation],
    ['abiPrimarySlotShapeBodyModel', row.calleeAbiPrimaryEntrySlotShapeBodyRelocationModel],
    ['abiPrimarySlotShapeBodyStatus', row.calleeAbiPrimaryEntrySlotShapeBodyRelocationStatus],
    ['abiPrimarySlotShapeBodyAction', row.calleeAbiPrimaryEntrySlotShapeBodyRelocationAction],
    ['abiPrimarySlotShapeTempCopy', row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopy],
    ['abiPrimarySlotShapeTempCopyModel',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel],
    ['abiPrimarySlotShapeTempCopyCost',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown],
    ['abiPrimarySlotShapeTempCopyNet',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNet],
    ['abiPrimarySlotShapeTempCopyStatus',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus],
    ['abiPrimarySlotShapeTempCopyAction',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction],
    ['abiPrimaryModeledPlacement', row.calleeAbiPrimaryEntryModeledPlacement],
    ['abiPrimaryModeledPlacementCost', row.calleeAbiPrimaryEntryModeledPlacementCostBreakdown],
    ['abiPrimaryModeledPlacementNet', row.calleeAbiPrimaryEntryModeledPlacementNet],
    ['abiPrimaryModeledPlacementStatus', row.calleeAbiPrimaryEntryModeledPlacementStatus],
    ['abiPrimaryModeledPlacementAction', row.calleeAbiPrimaryEntryModeledPlacementRequiredAction],
    ['abiPrimaryPlacement', row.calleeAbiPrimaryEntryPlacementLowerBoundByCallee],
    ['abiPrimaryPlacementNet', row.calleeAbiPrimaryEntryPlacementNet],
    ['abiPrimaryPlacementStatus', row.calleeAbiPrimaryEntryPlacementStatus],
  ];
  const details = fields
    .filter(([, value]) => value !== undefined && value !== null && value !== '')
    .map(([name, value]) => `${name}=${value}`);
  return details.length === 0 ? '' : ` ${details.join(' ')}`;
}

function helperInputSummary(row) {
  return row.suggestedResidentInputs ||
    row.profitableInputs ||
    row.breakEvenInputs ||
    row.unprofitableInputs ||
    '-';
}

function positiveInteger(value) {
  const parsed = Number(value);
  return Number.isInteger(parsed) && parsed > 0 ? parsed : null;
}

function helperPositiveGap(row) {
  return positiveInteger(row.calleeAbiNearPositiveGap) ??
    positiveInteger(row.calleeAbiBestSubsetNearPositiveGap) ??
    positiveInteger(row.calleeAbiAdditionalNetToPositive) ??
    positiveInteger(row.calleeAbiBestSubsetNeed) ??
    positiveInteger(row.breakEvenInputNeed) ??
    positiveInteger(row.bestInputAdditionalRecallCellsToProfit);
}

function printHelper(row) {
  console.log(
    `- ${row.savings} ${row.file} :: ${row.helper}` +
      ` traffic=${row.registerTrafficCells}` +
      ` shape=${row.trafficShape || '-'}` +
      ` plan=${row.planStatus || '-'}` +
      ` required=${row.requiredAction || '-'}` +
      ` cost=${row.costModelAction || '-'}` +
      ` inputs=${helperInputSummary(row)}` +
      ` breakEven=${row.breakEvenInputs || '-'}` +
      ` breakEvenNeed=${row.breakEvenInputNeed || '-'}` +
      ` breakEvenTarget=${row.breakEvenInputNextProofTarget || '-'}` +
      ` breakEvenAction=${row.breakEvenInputRequiredAction || '-'}` +
      ` bestInput=${row.bestInput || '-'}` +
      ` bestNet=${row.bestInputNet || '-'}` +
      ` need=${row.bestInputAdditionalRecallCellsToProfit || '-'}` +
      ` bestGap=${row.bestInputGapReason || '-'}` +
      ` bestTarget=${row.bestInputNextProofTarget || '-'}` +
      ` profit=${row.profitBreakdown || '-'}` +
      ` materialize=${row.materializeCellsByName || '-'}` +
      ` currentXMat=${row.currentXMaterializeCellsByName || '-'}` +
      ` currentXSites=${row.currentXMaterializeSites || '-'}` +
      ` currentXRetained=${row.currentXRetainedStores || '-'}` +
      ` currentXRetainedSites=${row.currentXRetainedSites || '-'}` +
      ` currentXRetainedReasons=${row.currentXRetainedReasons || '-'}` +
      ` currentXProof=${row.currentXMaterializeProof || '-'}` +
      ` currentXAction=${row.currentXMaterializeAction || '-'}` +
      ` argStores=${row.callerArgStoreCellsByName || '-'}` +
      ` argStoreNet=${row.callerArgStoreAdjustedNet || '-'}` +
      ` argStorePlan=${row.callerArgStorePlanStatus || '-'}` +
      ` argStoreAction=${row.callerArgStoreRequiredAction || '-'}` +
      ` argRecalls=${row.valueAwareArgumentRecallSites || '-'}` +
      ` repeatedArgRecalls=${row.valueAwareRepeatedArgumentRecallSites || '-'}` +
      ` topRepeatedArg=${row.valueAwareTopRepeatedArgumentRecall || '-'}` +
      ` schedulerTarget=${row.valueAwareSchedulerNextMaterializationTarget || '-'}` +
      ` schedulerFeasibility=${row.valueAwareRepeatedArgumentSchedulerFeasibility || '-'}` +
      ` schedulerStatus=${row.valueAwareRepeatedArgumentSchedulerStatus || '-'}` +
      ` schedulerAction=${row.valueAwareRepeatedArgumentSchedulerAction || '-'}` +
      ` argPreserve=${row.callArgumentPreservationCellsByCallee || '-'}` +
      ` argPreserveRaw=${row.callArgumentPreservationRaw || '-'}` +
      ` argZeroCopy=${row.callArgumentPreservationZeroCopy || '-'}` +
      ` argZeroCopyStatus=${row.callArgumentPreservationZeroCopyStatus || '-'}` +
      ` argZeroCopySites=${row.callArgumentPreservationZeroCopySites || '-'}` +
      ` argZeroCopyBlockers=${row.callArgumentPreservationZeroCopyBlockers || '-'}` +
      ` argPreserveLower=${row.callArgumentPreservationLowerBound || '-'}` +
      ` argPreserveBasis=${row.callArgumentPreservationLowerBoundBasis || '-'}` +
      ` argPreserveAction=${row.callArgumentPreservationRequiredAction || '-'}` +
      ` argX2=${row.callArgumentX2RestoreStatus || '-'}` +
      ` argX2Action=${row.callArgumentX2RequiredAction || '-'}` +
      ` argInputs=${row.callArgumentInputNamesByCallee || '-'}` +
      ` argSites=${row.callArgumentSites || '-'}` +
      ` preserveSites=${row.callPreservationSites || '-'}` +
      ` entryStack=${row.symbolicEntryStackSeed || '-'}` +
      ` entryStackStatus=${row.symbolicEntryStackSeedStatus || '-'}` +
      ` entryStackSites=${row.symbolicEntryStackByCallSite || '-'}` +
      ` knownCallee=${row.symbolicKnownCalleeStackEffects || '-'}` +
      ` flowEntry=${row.symbolicFlowEntryStack || '-'}` +
      ` lostEntryFacts=${row.entryStackLostKnownFacts || '-'}` +
      ` lostEntryFactCount=${row.entryStackLostKnownFactCount || '-'}` +
      ` lostEntrySlots=${row.entryStackLostKnownFactSlots || '-'}` +
      ` lostEntryStatus=${row.entryStackLostKnownFactStatus || '-'}` +
      ` lostEntryTarget=${row.entryStackLostKnownFactTarget || '-'}` +
      ` lostEntryAction=${row.entryStackLostKnownFactAction || '-'}` +
      ` entryInputs=${row.existingEntryStackInputSites || '-'}` +
      ` selectedStack=${row.selectedStackCarriedInputs || '-'}` +
      ` selectedStackStatus=${row.selectedStackCarriedStatus || '-'}` +
      ` selectedStackTargets=${row.selectedStackCarriedTargets || '-'}` +
      ` selectedStackSites=${row.selectedStackCarriedSites || '-'}` +
      ` mutating=${row.callPreservationMutatingOpcodes || '-'}` +
      ` x2=${row.callPreservationCalleeX2Effects || '-'}` +
      ` x2Mutating=${row.callArgumentX2MutationOpcodesByCallee || '-'}` +
      ` x2Class=${row.callArgumentX2ClobberClassesByCallee || '-'}` +
      ` x2Preload=${row.callArgumentX2PreloadConstantOpcodesByCallee || '-'}` +
      ` x2PreloadLiteral=${row.callArgumentX2PreloadLiteralReplacementByCallee || '-'}` +
      ` x2PreloadLiteralStatus=${row.callArgumentX2PreloadLiteralReplacementStatus || '-'}` +
      ` x2PreloadLiteralDelta=${row.callArgumentX2PreloadLiteralReplacementDelta || '-'}` +
      ` x2PreloadLiteralNet=${row.callArgumentX2PreloadLiteralReplacementNet || '-'}` +
      ` x2PreloadAction=${row.callArgumentX2PreloadRefactorRequiredAction || '-'}` +
      ` directMat=${row.directMaterializationStatus || '-'}:${row.directMaterialization || '-'}` +
      ` survival=${row.calleeStackSurvival || '-'}` +
      ` natural=${row.calleeNaturalPreservedSlots || '-'}` +
      ` restore=${row.calleeNaturalRestoreCells || '-'}` +
      ` restoreMin=${row.calleeNaturalMinRestoreCells || '-'}` +
      ` firstRecall=${row.calleeNaturalFirstRecallCoverage || '-'}` +
      ` firstRecallStatus=${row.calleeNaturalFirstRecallStatus || '-'}` +
      ` naturalChoice=${row.calleeNaturalFirstRecallChoice || '-'}` +
      ` naturalChoiceStatus=${row.calleeNaturalFirstRecallChoiceStatus || '-'}` +
      ` naturalFinalSlots=${row.calleeNaturalPreservedSlotFinalSlots || '-'}` +
      ` naturalUseShape=${row.calleeNaturalFirstRecallUseShape || '-'}` +
      ` naturalUseShapeStatus=${row.calleeNaturalFirstRecallUseShapeStatus || '-'}` +
      ` naturalUseShapeAction=${row.calleeNaturalFirstRecallUseShapeAction || '-'}` +
      ` naturalChoiceSearch=${row.calleeNaturalFirstRecallChoiceSearch || '-'}` +
      ` naturalChoiceSearchStatus=${row.calleeNaturalFirstRecallChoiceSearchStatus || '-'}` +
      ` remaining=${row.calleeRemainingPreserveDepth || '-'}` +
      ` abiPureStack=${row.calleeAbiPureStackPlacement || '-'}` +
      ` abiPureStackStatus=${row.calleeAbiPureStackPlacementStatus || '-'}` +
      ` abiProofDisposition=${row.calleeAbiPrimaryEntryProofDisposition || '-'}` +
      ` abiCost=${row.calleeAbiCostBreakdown || '-'}` +
      ` abiNet=${row.calleeAbiNetAfterLowerBound || '-'}` +
      ` abiNeed=${row.calleeAbiAdditionalNetToPositive || '-'}` +
      ` abiLevers=${row.calleeAbiPositiveLevers || '-'}` +
      ` abiGap=${row.calleeAbiPositiveGapReason || '-'}` +
      ` abiTarget=${row.calleeAbiNextProofTarget || '-'}` +
      ` abiNear=${row.calleeAbiNearPositiveStatus || '-'}` +
      ` abiNearGap=${row.calleeAbiNearPositiveGap || '-'}` +
      ` abiNearNet=${row.calleeAbiNearPositiveCurrentNet || '-'}` +
      ` abiNearPrimaryNet=${row.calleeAbiNearPositivePrimaryNet || '-'}` +
      ` abiNearPrimaryStatus=${row.calleeAbiNearPositivePrimaryStatus || '-'}` +
      ` abiNearReason=${row.calleeAbiNearPositiveReason || '-'}` +
      ` abiNearAction=${row.calleeAbiNearPositiveAction || '-'}` +
      ` abiNearProof=${row.calleeAbiNearPositiveProofStatus || '-'}` +
      ` abiNearPlacement=${row.calleeAbiNearPositiveStackPlacementStatus || '-'}` +
      ` abiNearPlacementBasis=${row.calleeAbiNearPositiveStackPlacementBasis || '-'}` +
      ` abiNearPlacementAction=${row.calleeAbiNearPositiveStackPlacementRequiredAction || '-'}` +
      ` abiNearSurvivorLimit=${row.calleeAbiNearPositiveNaturalSurvivorLimit || '-'}` +
      ` abiNearCost=${row.calleeAbiNearPositiveCurrentCost || '-'}` +
      ` abiNearPrimaryCost=${row.calleeAbiNearPositivePrimaryCost || '-'}` +
      ` abiSubsets=${row.calleeAbiSubsetCandidates || '-'}` +
      ` abiSubset=${row.calleeAbiBestSubset || '-'}` +
      ` abiSubsetNet=${row.calleeAbiBestSubsetNet || '-'}` +
      ` abiSubsetNeed=${row.calleeAbiBestSubsetNeed || '-'}` +
      ` abiSubsetCost=${row.calleeAbiBestSubsetCostBreakdown || '-'}` +
      ` abiSubsetLevers=${row.calleeAbiBestSubsetLevers || '-'}` +
      ` abiSubsetGap=${row.calleeAbiBestSubsetGapReason || '-'}` +
      ` abiSubsetTarget=${row.calleeAbiBestSubsetNextProofTarget || '-'}` +
      ` abiSubsetStatus=${row.calleeAbiBestSubsetStatus || '-'}` +
      ` abiSubsetNear=${row.calleeAbiBestSubsetNearPositiveStatus || '-'}` +
      ` abiSubsetNearGap=${row.calleeAbiBestSubsetNearPositiveGap || '-'}` +
      ` abiSubsetNearNet=${row.calleeAbiBestSubsetNearPositiveCurrentNet || '-'}` +
      ` abiSubsetNearPrimaryNet=${row.calleeAbiBestSubsetNearPositivePrimaryNet || '-'}` +
      ` abiSubsetNearPrimaryStatus=${row.calleeAbiBestSubsetNearPositivePrimaryStatus || '-'}` +
      ` abiSubsetNearReason=${row.calleeAbiBestSubsetNearPositiveReason || '-'}` +
      ` abiSubsetNearAction=${row.calleeAbiBestSubsetNearPositiveAction || '-'}` +
      ` abiSubsetNearProof=${row.calleeAbiBestSubsetNearPositiveProofStatus || '-'}` +
      ` abiSubsetNearPlacement=${row.calleeAbiBestSubsetNearPositiveStackPlacementStatus || '-'}` +
      ` abiSubsetNearPlacementBasis=${row.calleeAbiBestSubsetNearPositiveStackPlacementBasis || '-'}` +
      ` abiSubsetNearPlacementAction=${row.calleeAbiBestSubsetNearPositiveStackPlacementRequiredAction || '-'}` +
      ` abiSubsetNearSurvivorLimit=${row.calleeAbiBestSubsetNearPositiveNaturalSurvivorLimit || '-'}` +
      ` abiSubsetNearCost=${row.calleeAbiBestSubsetNearPositiveCurrentCost || '-'}` +
      ` abiSubsetNearPrimaryCost=${row.calleeAbiBestSubsetNearPositivePrimaryCost || '-'}` +
      ` abiPrimaryCoverage=${row.calleeAbiPrimaryEntryCoverage || '-'}` +
      ` abiPrimaryEligible=${row.calleeAbiPrimaryEntryEligibleTargets || '-'}` +
      ` abiPrimaryBlocked=${row.calleeAbiPrimaryEntryBlockedTargets || '-'}` +
      ` abiPrimaryProtocol=${row.calleeAbiPrimaryEntryProtocol || '-'}` +
      ` abiPrimaryProtocolStatus=${row.calleeAbiPrimaryEntryProtocolStatus || '-'}` +
      ` abiPrimaryProtocolAction=${row.calleeAbiPrimaryEntryProtocolAction || '-'}` +
      ` abiPrimaryArgRestage=${row.calleeAbiPrimaryEntryArgumentRestage || '-'}` +
      ` abiPrimaryArgRestageSites=${row.calleeAbiPrimaryEntryArgumentRestageSites || '-'}` +
      ` abiPrimarySiteModel=${row.calleeAbiPrimaryEntryPlacementSiteModel || '-'}` +
      ` abiPrimarySiteStatus=${row.calleeAbiPrimaryEntryPlacementSiteModelStatus || '-'}` +
      ` abiPrimarySiteAction=${row.calleeAbiPrimaryEntryPlacementSiteModelAction || '-'}` +
      ` abiPrimaryPreCallPlacement=${row.calleeAbiPrimaryEntryPreCallPlacement || '-'}` +
      ` abiPrimaryPreCallPlacementStatus=${row.calleeAbiPrimaryEntryPreCallPlacementStatus || '-'}` +
      ` abiPrimaryPreCallPlacementAction=${row.calleeAbiPrimaryEntryPreCallPlacementAction || '-'}` +
      ` abiPrimaryPreCallProof=${row.calleeAbiPrimaryEntryPreCallPlacementProof || '-'}` +
      ` abiPrimaryPreCallProofStatus=${row.calleeAbiPrimaryEntryPreCallPlacementProofStatus || '-'}` +
      ` abiPrimaryPreCallProofAction=${row.calleeAbiPrimaryEntryPreCallPlacementProofAction || '-'}` +
      ` abiPrimaryPreCallRewrite=${row.calleeAbiPrimaryEntryPreCallPlacementRewriteEstimate || '-'}` +
      ` abiPrimaryPreCallRewriteStatus=${row.calleeAbiPrimaryEntryPreCallPlacementRewriteStatus || '-'}` +
      ` abiPrimaryPreCallRewriteModel=${row.calleeAbiPrimaryEntryPreCallPlacementRewriteModel || '-'}` +
      ` abiPrimaryPreCallRewriteAction=${row.calleeAbiPrimaryEntryPreCallPlacementRewriteAction || '-'}` +
      ` abiPrimarySlotSearch=${row.calleeAbiPrimaryEntrySlotSearch || '-'}` +
      ` abiPrimarySlotSearchStatus=${row.calleeAbiPrimaryEntrySlotSearchStatus || '-'}` +
      ` abiPrimarySlotSearchAction=${row.calleeAbiPrimaryEntrySlotSearchAction || '-'}` +
      ` abiPrimarySlotSearchActionByCallee=${row.calleeAbiPrimaryEntrySlotSearchActionByCallee || '-'}` +
      ` abiPrimarySlotShape=${row.calleeAbiPrimaryEntrySlotShapeCandidate || '-'}` +
      ` abiPrimarySlotShapeRewriteCells=${row.calleeAbiPrimaryEntrySlotShapePreCallRewriteCells || '-'}` +
      ` abiPrimarySlotShapeCalleeCells=${row.calleeAbiPrimaryEntrySlotShapeCalleeLowerBoundCells || '-'}` +
      ` abiPrimarySlotShapeCells=${row.calleeAbiPrimaryEntrySlotShapeModeledPlacementCells || '-'}` +
      ` abiPrimarySlotShapeBasis=${row.calleeAbiPrimaryEntrySlotShapeBasis || '-'}` +
      ` abiPrimarySlotShapeCost=${row.calleeAbiPrimaryEntrySlotShapeCostBreakdown || '-'}` +
      ` abiPrimarySlotShapeNet=${row.calleeAbiPrimaryEntrySlotShapeNet || '-'}` +
      ` abiPrimarySlotShapeNeed=${row.calleeAbiPrimaryEntrySlotShapeNeed || '-'}` +
      ` abiPrimarySlotShapeStatus=${row.calleeAbiPrimaryEntrySlotShapeStatus || '-'}` +
      ` abiPrimarySlotShapeModelStatus=${row.calleeAbiPrimaryEntrySlotShapeModelStatus || '-'}` +
      ` abiPrimarySlotShapeAction=${row.calleeAbiPrimaryEntrySlotShapeAction || '-'}` +
      ` abiPrimarySlotShapeActionByCallee=${row.calleeAbiPrimaryEntrySlotShapeActionByCallee || '-'}` +
      ` abiPrimarySlotShapeRequiredAction=${row.calleeAbiPrimaryEntrySlotShapeRequiredAction || '-'}` +
      ` abiPrimarySlotShapeSafeFallback=${row.calleeAbiPrimaryEntrySlotShapeSafeFallback || '-'}` +
      ` abiPrimarySlotShapeSafeFallbackStatus=${row.calleeAbiPrimaryEntrySlotShapeSafeFallbackStatus || '-'}` +
      ` abiPrimarySlotShapeSafeFallbackAction=${row.calleeAbiPrimaryEntrySlotShapeSafeFallbackAction || '-'}` +
      ` abiPrimarySlotShapeRole=${row.calleeAbiPrimaryEntrySlotShapeRoleRequirement || '-'}` +
      ` abiPrimarySlotShapeRoleStatus=${row.calleeAbiPrimaryEntrySlotShapeRoleRequirementStatus || '-'}` +
      ` abiPrimarySlotShapeRoleAction=${row.calleeAbiPrimaryEntrySlotShapeRoleRequirementAction || '-'}` +
      ` abiPrimarySlotShapeRoleActionByCallee=${row.calleeAbiPrimaryEntrySlotShapeRoleRequirementActionByCallee || '-'}` +
      ` abiPrimarySlotShapeBody=${row.calleeAbiPrimaryEntrySlotShapeBodyRelocation || '-'}` +
      ` abiPrimarySlotShapeBodyModel=${row.calleeAbiPrimaryEntrySlotShapeBodyRelocationModel || '-'}` +
      ` abiPrimarySlotShapeBodyStatus=${row.calleeAbiPrimaryEntrySlotShapeBodyRelocationStatus || '-'}` +
      ` abiPrimarySlotShapeBodyAction=${row.calleeAbiPrimaryEntrySlotShapeBodyRelocationAction || '-'}` +
      ` abiPrimarySlotShapeBodyActionByCallee=${row.calleeAbiPrimaryEntrySlotShapeBodyRelocationActionByCallee || '-'}` +
      ` abiPrimarySlotShapeTempCopy=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopy || '-'}` +
      ` abiPrimarySlotShapeTempCopyCells=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCells || '-'}` +
      ` abiPrimarySlotShapeTempCopyModel=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel || '-'}` +
      ` abiPrimarySlotShapeTempCopyStatus=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyStatus || '-'}` +
      ` abiPrimarySlotShapeTempCopyAction=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyAction || '-'}` +
      ` abiPrimarySlotShapeTempCopyActionByCallee=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyActionByCallee || '-'}` +
      ` abiPrimarySlotShapeTempCopyCost=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown || '-'}` +
      ` abiPrimarySlotShapeTempCopyNet=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNet || '-'}` +
      ` abiPrimarySlotShapeTempCopyNeed=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNeed || '-'}` +
      ` abiPrimarySlotShapeTempCopyModelStatus=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus || '-'}` +
      ` abiPrimarySlotShapeTempCopyRequiredAction=${row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction || '-'}` +
      ` abiPrimaryPlacement=${row.calleeAbiPrimaryEntryPlacementLowerBoundByCallee || '-'}` +
      ` abiPrimaryPlacementBasis=${row.calleeAbiPrimaryEntryPlacementLowerBoundBasis || '-'}` +
      ` abiPrimaryModeledPlacement=${row.calleeAbiPrimaryEntryModeledPlacement || '-'}` +
      ` abiPrimaryModeledPlacementCells=${row.calleeAbiPrimaryEntryModeledPlacementCells || '-'}` +
      ` abiPrimaryModeledPlacementBasis=${row.calleeAbiPrimaryEntryModeledPlacementBasis || '-'}` +
      ` abiPrimaryCost=${row.calleeAbiPrimaryEntryCostBreakdown || '-'}` +
      ` abiPrimaryPlacementCost=${row.calleeAbiPrimaryEntryPlacementCostBreakdown || '-'}` +
      ` abiPrimaryModeledPlacementCost=${row.calleeAbiPrimaryEntryModeledPlacementCostBreakdown || '-'}` +
      ` abiPrimaryNet=${row.calleeAbiPrimaryEntryNet || '-'}` +
      ` abiPrimaryPlacementNet=${row.calleeAbiPrimaryEntryPlacementNet || '-'}` +
      ` abiPrimaryModeledPlacementNet=${row.calleeAbiPrimaryEntryModeledPlacementNet || '-'}` +
      ` abiPrimaryNeed=${row.calleeAbiPrimaryEntryNeed || '-'}` +
      ` abiPrimaryPlacementNeed=${row.calleeAbiPrimaryEntryPlacementNeed || '-'}` +
      ` abiPrimaryModeledPlacementNeed=${row.calleeAbiPrimaryEntryModeledPlacementNeed || '-'}` +
      ` abiPrimaryStatus=${row.calleeAbiPrimaryEntryStatus || '-'}` +
      ` abiPrimaryPlacementStatus=${row.calleeAbiPrimaryEntryPlacementStatus || '-'}` +
      ` abiPrimaryModeledPlacementStatus=${row.calleeAbiPrimaryEntryModeledPlacementStatus || '-'}` +
      ` abiPrimaryModeledPlacementAction=${row.calleeAbiPrimaryEntryModeledPlacementRequiredAction || '-'}` +
      ` abiSubsetPrimaryCost=${row.calleeAbiBestSubsetPrimaryEntryCostBreakdown || '-'}` +
      ` abiSubsetPrimaryPlacementCost=${row.calleeAbiBestSubsetPrimaryEntryPlacementCostBreakdown || '-'}` +
      ` abiSubsetPrimaryNet=${row.calleeAbiBestSubsetPrimaryEntryNet || '-'}` +
      ` abiSubsetPrimaryPlacement=${row.calleeAbiBestSubsetPrimaryEntryPlacementLowerBound || '-'}` +
      ` abiSubsetPrimaryPlacementNet=${row.calleeAbiBestSubsetPrimaryEntryPlacementNet || '-'}` +
      ` abiSubsetPrimaryNeed=${row.calleeAbiBestSubsetPrimaryEntryNeed || '-'}` +
      ` abiSubsetPrimaryPlacementNeed=${row.calleeAbiBestSubsetPrimaryEntryPlacementNeed || '-'}` +
      ` abiSubsetPrimaryStatus=${row.calleeAbiBestSubsetPrimaryEntryStatus || '-'}` +
      ` abiSubsetPrimaryPlacementStatus=${row.calleeAbiBestSubsetPrimaryEntryPlacementStatus || '-'}` +
      ` callee=${row.calleeAbiStatus || '-'}`,
  );
}

function printAction(row) {
  console.log(
    `- ${row.status} potential=${row.potentialSavings} best=${row.bestSavings}` +
      ` ${row.file} :: ${row.source}=${row.action}` +
      ` variant=${row.bestVariant || '-'}` +
      ` blocker=${row.bestBlockerKind || '-'}` +
      ` helper=${row.helper || '-'}` +
      actionDetailSuffix(row),
  );
}

function printActionSummary(row) {
  const fields = [
    ['profitableInputs', row.profitableInputs],
    ['breakEven', row.breakEvenInputs],
    ['breakEvenCount', row.breakEvenInputCount],
    ['breakEvenGross', row.breakEvenInputGross],
    ['breakEvenMaterialize', row.breakEvenInputMaterialize],
    ['breakEvenNet', row.breakEvenInputNet],
    ['breakEvenStatus', row.breakEvenInputStatus],
    ['breakEvenNeed', row.breakEvenInputNeed],
    ['breakEvenGap', row.breakEvenInputGapReason],
    ['breakEvenTarget', row.breakEvenInputNextProofTarget],
    ['breakEvenAction', row.breakEvenInputRequiredAction],
    ['unprofitableInputs', row.unprofitableInputs],
    ['bestInput', row.bestInput],
    ['bestNet', row.bestInputNet],
    ['bestNeed', row.bestInputAdditionalRecallCellsToProfit],
    ['bestGap', row.bestInputGapReason],
    ['bestTarget', row.bestInputNextProofTarget],
    ['argX2', row.callArgumentX2RestoreStatus],
    ['x2Class', row.callArgumentX2ClobberClassesByCallee],
    ['x2Preload', row.callArgumentX2PreloadConstantOpcodesByCallee],
    ['x2PreloadLiteral', row.callArgumentX2PreloadLiteralReplacementByCallee],
    ['x2PreloadLiteralStatus', row.callArgumentX2PreloadLiteralReplacementStatus],
    ['x2PreloadLiteralDelta', row.callArgumentX2PreloadLiteralReplacementDelta],
    ['x2PreloadLiteralNet', row.callArgumentX2PreloadLiteralReplacementNet],
    ['x2PreloadAction', row.callArgumentX2PreloadRefactorRequiredAction],
    ['abiNearPrimaryNet', row.calleeAbiNearPositivePrimaryNet],
    ['abiPlacement', row.calleeAbiNearPositiveStackPlacementStatus],
    ['abiPlacementAction', row.calleeAbiNearPositiveStackPlacementRequiredAction],
    ['abiPureStack', row.calleeAbiPureStackPlacement],
    ['abiProofDisposition', row.calleeAbiPrimaryEntryProofDisposition],
    ['abiCost', row.calleeAbiCostBreakdown],
    ['abiNet', row.calleeAbiNetAfterLowerBound],
    ['abiNeed', row.calleeAbiAdditionalNetToPositive],
    ['abiLevers', row.calleeAbiPositiveLevers],
    ['abiGap', row.calleeAbiPositiveGapReason],
    ['abiTarget', row.calleeAbiNextProofTarget],
    ['argPreserveRaw', row.callArgumentPreservationRaw],
    ['argZeroCopy', row.callArgumentPreservationZeroCopy],
    ['argZeroCopyStatus', row.callArgumentPreservationZeroCopyStatus],
    ['argZeroCopySites', row.callArgumentPreservationZeroCopySites],
    ['argZeroCopyBlockers', row.callArgumentPreservationZeroCopyBlockers],
    ['entryStack', row.symbolicEntryStackSeed],
    ['entryStackStatus', row.symbolicEntryStackSeedStatus],
    ['entryStackSites', row.symbolicEntryStackByCallSite],
    ['knownCallee', row.symbolicKnownCalleeStackEffects],
    ['flowEntry', row.symbolicFlowEntryStack],
    ['lostEntryFacts', row.entryStackLostKnownFacts],
    ['lostEntryFactCount', row.entryStackLostKnownFactCount],
    ['lostEntrySlots', row.entryStackLostKnownFactSlots],
    ['lostEntryStatus', row.entryStackLostKnownFactStatus],
    ['lostEntryTarget', row.entryStackLostKnownFactTarget],
    ['lostEntryAction', row.entryStackLostKnownFactAction],
    ['entryInputs', row.existingEntryStackInputSites],
    ['selectedStack', row.selectedStackCarriedInputs],
    ['selectedStackStatus', row.selectedStackCarriedStatus],
    ['selectedStackTargets', row.selectedStackCarriedTargets],
    ['selectedStackSites', row.selectedStackCarriedSites],
    ['abiProtocol', row.calleeAbiPrimaryEntryProtocol],
    ['abiArgRestage', row.calleeAbiPrimaryEntryArgumentRestage],
    ['argRecalls', row.valueAwareArgumentRecallSites],
    ['repeatedArgRecalls', row.valueAwareRepeatedArgumentRecallSites],
    ['topRepeatedArg', row.valueAwareTopRepeatedArgumentRecall],
    ['schedulerTarget', row.valueAwareSchedulerNextMaterializationTarget],
    ['schedulerFeasibility', row.valueAwareRepeatedArgumentSchedulerFeasibility],
    ['schedulerStatus', row.valueAwareRepeatedArgumentSchedulerStatus],
    ['schedulerAction', row.valueAwareRepeatedArgumentSchedulerAction],
    ['schedulerProfitableNet', row.valueAwareRepeatedArgumentSchedulerProfitableNet],
    ['schedulerModelNet', row.valueAwareRepeatedArgumentSchedulerModelNet],
    ['abiSubsets', row.calleeAbiSubsetCandidates],
    ['abiSubset', row.calleeAbiBestSubset],
    ['abiSubsetNet', row.calleeAbiBestSubsetNet],
    ['abiSubsetNeed', row.calleeAbiBestSubsetNeed],
    ['abiSubsetCost', row.calleeAbiBestSubsetCostBreakdown],
    ['abiSubsetLevers', row.calleeAbiBestSubsetLevers],
    ['abiSubsetGap', row.calleeAbiBestSubsetGapReason],
    ['abiSubsetTarget', row.calleeAbiBestSubsetNextProofTarget],
    ['abiSubsetStatus', row.calleeAbiBestSubsetStatus],
    ['naturalChoice', row.calleeNaturalFirstRecallChoice],
    ['naturalChoiceStatus', row.calleeNaturalFirstRecallChoiceStatus],
    ['naturalFinalSlots', row.calleeNaturalPreservedSlotFinalSlots],
    ['naturalUseShape', row.calleeNaturalFirstRecallUseShape],
    ['naturalUseShapeStatus', row.calleeNaturalFirstRecallUseShapeStatus],
    ['naturalUseShapeAction', row.calleeNaturalFirstRecallUseShapeAction],
    ['naturalChoiceSearch', row.calleeNaturalFirstRecallChoiceSearch],
    ['naturalChoiceSearchStatus', row.calleeNaturalFirstRecallChoiceSearchStatus],
    ['abiPrimarySiteModel', row.calleeAbiPrimaryEntryPlacementSiteModel],
    ['abiPrimarySiteStatus', row.calleeAbiPrimaryEntryPlacementSiteModelStatus],
    ['abiPrimaryPreCallPlacement', row.calleeAbiPrimaryEntryPreCallPlacement],
    ['abiPrimaryPreCallPlacementStatus', row.calleeAbiPrimaryEntryPreCallPlacementStatus],
    ['abiPrimaryPreCallPlacementAction', row.calleeAbiPrimaryEntryPreCallPlacementAction],
    ['abiPrimaryPreCallProof', row.calleeAbiPrimaryEntryPreCallPlacementProof],
    ['abiPrimaryPreCallProofStatus', row.calleeAbiPrimaryEntryPreCallPlacementProofStatus],
    ['abiPrimaryPreCallProofAction', row.calleeAbiPrimaryEntryPreCallPlacementProofAction],
    ['abiPrimaryPreCallRewrite', row.calleeAbiPrimaryEntryPreCallPlacementRewriteEstimate],
    ['abiPrimaryPreCallRewriteStatus', row.calleeAbiPrimaryEntryPreCallPlacementRewriteStatus],
    ['abiPrimaryPreCallRewriteModel', row.calleeAbiPrimaryEntryPreCallPlacementRewriteModel],
    ['abiPrimaryPreCallRewriteAction', row.calleeAbiPrimaryEntryPreCallPlacementRewriteAction],
    ['abiPrimarySlotSearch', row.calleeAbiPrimaryEntrySlotSearch],
    ['abiPrimarySlotSearchStatus', row.calleeAbiPrimaryEntrySlotSearchStatus],
    ['abiPrimarySlotSearchAction', row.calleeAbiPrimaryEntrySlotSearchAction],
    ['abiPrimarySlotShape', row.calleeAbiPrimaryEntrySlotShapeCandidate],
    ['abiPrimarySlotShapeCost', row.calleeAbiPrimaryEntrySlotShapeCostBreakdown],
    ['abiPrimarySlotShapeNet', row.calleeAbiPrimaryEntrySlotShapeNet],
    ['abiPrimarySlotShapeStatus', row.calleeAbiPrimaryEntrySlotShapeModelStatus],
    ['abiPrimarySlotShapeAction', row.calleeAbiPrimaryEntrySlotShapeRequiredAction],
    ['abiPrimarySlotShapeSafeFallback', row.calleeAbiPrimaryEntrySlotShapeSafeFallback],
    ['abiPrimarySlotShapeSafeFallbackStatus',
      row.calleeAbiPrimaryEntrySlotShapeSafeFallbackStatus],
    ['abiPrimarySlotShapeSafeFallbackAction',
      row.calleeAbiPrimaryEntrySlotShapeSafeFallbackAction],
    ['abiPrimarySlotShapeRole', row.calleeAbiPrimaryEntrySlotShapeRoleRequirement],
    ['abiPrimarySlotShapeRoleStatus',
      row.calleeAbiPrimaryEntrySlotShapeRoleRequirementStatus],
    ['abiPrimarySlotShapeRoleAction',
      row.calleeAbiPrimaryEntrySlotShapeRoleRequirementAction],
    ['abiPrimarySlotShapeBody', row.calleeAbiPrimaryEntrySlotShapeBodyRelocation],
    ['abiPrimarySlotShapeBodyModel', row.calleeAbiPrimaryEntrySlotShapeBodyRelocationModel],
    ['abiPrimarySlotShapeBodyStatus', row.calleeAbiPrimaryEntrySlotShapeBodyRelocationStatus],
    ['abiPrimarySlotShapeBodyAction', row.calleeAbiPrimaryEntrySlotShapeBodyRelocationAction],
    ['abiPrimarySlotShapeTempCopy', row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopy],
    ['abiPrimarySlotShapeTempCopyModel',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModel],
    ['abiPrimarySlotShapeTempCopyCost',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyCostBreakdown],
    ['abiPrimarySlotShapeTempCopyNet',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyNet],
    ['abiPrimarySlotShapeTempCopyStatus',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyModelStatus],
    ['abiPrimarySlotShapeTempCopyAction',
      row.calleeAbiPrimaryEntrySlotShapeExplicitTempCopyRequiredAction],
    ['abiPrimaryModeledPlacement', row.calleeAbiPrimaryEntryModeledPlacement],
    ['abiPrimaryModeledPlacementCost', row.calleeAbiPrimaryEntryModeledPlacementCostBreakdown],
    ['abiPrimaryModeledPlacementNet', row.calleeAbiPrimaryEntryModeledPlacementNet],
    ['abiPrimaryModeledPlacementStatus', row.calleeAbiPrimaryEntryModeledPlacementStatus],
    ['abiPrimaryModeledPlacementAction', row.calleeAbiPrimaryEntryModeledPlacementRequiredAction],
    ['abiPrimaryPlacement', row.calleeAbiPrimaryEntryPlacementLowerBoundByCallee],
    ['abiPrimaryPlacementNet', row.calleeAbiPrimaryEntryPlacementNet],
    ['abiPrimaryPlacementStatus', row.calleeAbiPrimaryEntryPlacementStatus],
  ];
  const suffix = fields
    .filter(([, value]) => value !== undefined && value !== null && value !== '')
    .map(([name, value]) => `${name}=${value}`)
    .join(' ');
  console.log(
    `- ${row.status} potential=${row.potentialSavings} best=${row.bestSavings}` +
      ` ${row.file} :: ${row.source}=${row.action}` +
      ` variant=${row.bestVariant || '-'}` +
      ` blocker=${row.bestBlockerKind || '-'}` +
      ` helper=${row.helper || '-'}` +
      (suffix ? ` ${suffix}` : ''),
  );
}

function printText(report) {
  console.log(`Analyzed files: ${report.analyzedFiles}`);
  console.log(`Total cells: ${report.totalCells}`);
  console.log(`Selected helper costs: ${report.helperCostCount}`);
  console.log(`Helper register-traffic opportunities: ${report.helperTrafficCount}`);
  console.log(`Candidate/rejected opportunities: ${report.candidateOpportunityCount}`);

  console.log('\nTop selected helper costs:');
  if (report.topHelpers.length === 0) {
    console.log('(none)');
  } else {
    report.topHelpers.slice(0, 20).forEach(printHelperCost);
  }

  console.log('\nPositive next actions:');
  if (report.positiveNextActions.length === 0) {
    console.log('(none)');
  } else {
    report.positiveNextActions.slice(0, 20).forEach(printAction);
  }

  console.log('\nNear-positive helper register-traffic opportunities:');
  if (report.nearPositiveHelperTraffic.length === 0) {
    console.log('(none)');
  } else {
    report.nearPositiveHelperTraffic.slice(0, 20).forEach((row) => {
      console.log(`  gap=${row.positiveGap}`);
      printHelper(row);
    });
  }

  console.log('\nPositive candidate/rejected opportunities:');
  if (report.positiveCandidateOpportunities.length === 0) {
    console.log('(none)');
  } else {
    report.positiveCandidateOpportunities.slice(0, 20).forEach(printCandidateOpportunity);
  }

  console.log('\nTop rejected/nonpositive candidate opportunities:');
  if (report.topRejectedCandidateOpportunities.length === 0) {
    console.log('(none)');
  } else {
    report.topRejectedCandidateOpportunities.slice(0, 20).forEach(printCandidateOpportunity);
  }

  console.log('\nCandidate/rejected blockers by group:');
  if (report.candidateBlockerGroups.length === 0) {
    console.log('(none)');
  } else {
    report.candidateBlockerGroups.slice(0, 20).forEach(printGroup);
  }

  console.log('\nStalled next actions by group:');
  const stalledGroups = report.nextActionGroups.filter((group) =>
    group.key.startsWith('stalled-'));
  if (stalledGroups.length === 0) {
    console.log('(none)');
  } else {
    stalledGroups.slice(0, 20).forEach(printGroup);
  }

  console.log('\nTop stalled next actions:');
  if (report.stalledNextActions.length === 0) {
    console.log('(none)');
  } else {
    report.stalledNextActions.slice(0, 20).forEach(printAction);
  }

  console.log('\nHelper traffic by plan/action:');
  if (report.helperPlanGroups.length === 0) {
    console.log('(none)');
  } else {
    report.helperPlanGroups.slice(0, 20).forEach(printGroup);
  }

  console.log('\nPositive helper register-traffic opportunities:');
  if (report.positiveHelperTraffic.length === 0) {
    console.log('(none)');
  } else {
    report.positiveHelperTraffic.slice(0, 20).forEach(printHelper);
  }

  console.log('\nTop stalled helper register-traffic opportunities:');
  if (report.stalledHelperTraffic.length === 0) {
    console.log('(none)');
  } else {
    report.stalledHelperTraffic.slice(0, 20).forEach(printHelper);
  }
}

function printSummaryText(report) {
  console.log(`Analyzed files: ${report.analyzedFiles}`);
  console.log(`Total cells: ${report.totalCells}`);
  console.log(`Selected helper costs: ${report.helperCostCount}`);
  console.log(`Helper register-traffic opportunities: ${report.helperTrafficCount}`);
  console.log(`Candidate/rejected opportunities: ${report.candidateOpportunityCount}`);

  console.log('\nPositive next actions:');
  if (report.positiveNextActions.length === 0) {
    console.log('(none)');
  } else {
    report.positiveNextActions.slice(0, 20).forEach(printActionSummary);
  }

  console.log('\nCandidate/rejected blockers by group:');
  if (report.candidateBlockerGroups.length === 0) {
    console.log('(none)');
  } else {
    report.candidateBlockerGroups.slice(0, 20).forEach(printGroup);
  }

  console.log('\nStalled next actions by group:');
  const stalledGroups = report.nextActionGroups.filter((group) =>
    group.key.startsWith('stalled-'));
  if (stalledGroups.length === 0) {
    console.log('(none)');
  } else {
    stalledGroups.slice(0, 20).forEach(printGroup);
  }

  console.log('\nHelper traffic by plan/action:');
  if (report.helperPlanGroups.length === 0) {
    console.log('(none)');
  } else {
    report.helperPlanGroups.slice(0, 20).forEach(printGroup);
  }

  console.log('\nTop stalled next actions:');
  if (report.stalledNextActions.length === 0) {
    console.log('(none)');
  } else {
    report.stalledNextActions.slice(0, 20).forEach(printActionSummary);
  }
}

function main() {
  try {
    const options = parseArgs(process.argv.slice(2));
    const inputPaths = options.inputs.length === 0 ? defaultInputs() : options.inputs;
    let files = inputPaths.flatMap(collectMkproFiles);
    files = [...new Set(files)].sort();
    if (options.limit !== null) {
      files = files.slice(0, options.limit);
    }
    if (files.length === 0) {
      throw new Error('no .mkpro files to analyze');
    }
    const rows = files.map((file) => analyzeFile(options.compiler, file));
    const report = aggregate(rows);
    if (options.json) {
      console.log(JSON.stringify(report, null, 2));
    } else if (options.summaryOnly) {
      printSummaryText(report);
    } else {
      printText(report);
    }
  } catch (error) {
    console.error(`size-opportunity-report: ${error.message}`);
    process.exit(1);
  }
}

main();
