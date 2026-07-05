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
    callCellsPerOccurrence: detail(helper.details, 'callCellsPerOccurrence'),
    nextPipelineAction: detail(helper.details, 'nextPipelineAction'),
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
      calleeAbiPrimaryEntryCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryCostBreakdown',
      ),
      calleeAbiPrimaryEntryPlacementCostBreakdown: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementCostBreakdown',
      ),
      calleeAbiPrimaryEntryNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterLowerBoundCells',
      ),
      calleeAbiPrimaryEntryPlacementNet: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryNetAfterPlacementLowerBoundCells',
      ),
      calleeAbiPrimaryEntryNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntryPlacementNeed: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementAdditionalNetCellsToPositive',
      ),
      calleeAbiPrimaryEntryStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryStatus',
      ),
      calleeAbiPrimaryEntryPlacementStatus: detail(
        details,
        'valueAwareCalleeAbiPrimaryEntryPlacementStatus',
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
      callArgumentPreservationCellsByCallee: detail(
        details,
        'valueAwareCallArgumentPreservationCellsByCallee',
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
      calleeNaturalFirstRecallChoice: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceByCallee',
      ),
      calleeNaturalFirstRecallChoiceStatus: detail(
        details,
        'valueAwareCalleeAbiNaturalFirstRecallChoiceStatusByCallee',
      ),
      calleeAbiCostBreakdown: detail(details, 'valueAwareCalleeAbiCostBreakdown'),
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
      ` variant=${best.bestVariant ?? best.variant ?? ''}`,
  );
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
      ` next=${row.nextPipelineAction || '-'}`,
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
      ` reason=${row.reason || '-'}`,
  );
}

function actionDetailSuffix(row) {
  const fields = [
    ['argPreserve', row.callArgumentPreservationCellsByCallee],
    ['argPreserveLower', row.callArgumentPreservationLowerBound],
    ['argPreserveBasis', row.callArgumentPreservationLowerBoundBasis],
    ['argPreserveAction', row.callArgumentPreservationRequiredAction],
    ['argX2', row.callArgumentX2RestoreStatus],
    ['argX2Action', row.callArgumentX2RequiredAction],
    ['x2Mutating', row.callArgumentX2MutationOpcodesByCallee],
    ['x2Class', row.callArgumentX2ClobberClassesByCallee],
    ['x2Preload', row.callArgumentX2PreloadConstantOpcodesByCallee],
    ['argInputs', row.callArgumentInputNamesByCallee],
    ['argSites', row.callArgumentSites],
    ['preserveSites', row.callPreservationSites],
    ['abiCost', row.calleeAbiCostBreakdown],
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
    ['naturalChoice', row.calleeNaturalFirstRecallChoice],
    ['naturalChoiceStatus', row.calleeNaturalFirstRecallChoiceStatus],
    ['abiPrimarySiteModel', row.calleeAbiPrimaryEntryPlacementSiteModel],
    ['abiPrimarySiteStatus', row.calleeAbiPrimaryEntryPlacementSiteModelStatus],
    ['abiPrimaryPreCallPlacement', row.calleeAbiPrimaryEntryPreCallPlacement],
    ['abiPrimaryPreCallPlacementStatus', row.calleeAbiPrimaryEntryPreCallPlacementStatus],
    ['abiPrimaryPreCallPlacementAction', row.calleeAbiPrimaryEntryPreCallPlacementAction],
    ['abiPrimaryPreCallProof', row.calleeAbiPrimaryEntryPreCallPlacementProof],
    ['abiPrimaryPreCallProofStatus', row.calleeAbiPrimaryEntryPreCallPlacementProofStatus],
    ['abiPrimaryPreCallProofAction', row.calleeAbiPrimaryEntryPreCallPlacementProofAction],
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
      ` argPreserve=${row.callArgumentPreservationCellsByCallee || '-'}` +
      ` argPreserveLower=${row.callArgumentPreservationLowerBound || '-'}` +
      ` argPreserveBasis=${row.callArgumentPreservationLowerBoundBasis || '-'}` +
      ` argPreserveAction=${row.callArgumentPreservationRequiredAction || '-'}` +
      ` argX2=${row.callArgumentX2RestoreStatus || '-'}` +
      ` argX2Action=${row.callArgumentX2RequiredAction || '-'}` +
      ` argInputs=${row.callArgumentInputNamesByCallee || '-'}` +
      ` argSites=${row.callArgumentSites || '-'}` +
      ` preserveSites=${row.callPreservationSites || '-'}` +
      ` mutating=${row.callPreservationMutatingOpcodes || '-'}` +
      ` x2=${row.callPreservationCalleeX2Effects || '-'}` +
      ` x2Mutating=${row.callArgumentX2MutationOpcodesByCallee || '-'}` +
      ` x2Class=${row.callArgumentX2ClobberClassesByCallee || '-'}` +
      ` x2Preload=${row.callArgumentX2PreloadConstantOpcodesByCallee || '-'}` +
      ` directMat=${row.directMaterializationStatus || '-'}:${row.directMaterialization || '-'}` +
      ` survival=${row.calleeStackSurvival || '-'}` +
      ` natural=${row.calleeNaturalPreservedSlots || '-'}` +
      ` restore=${row.calleeNaturalRestoreCells || '-'}` +
      ` restoreMin=${row.calleeNaturalMinRestoreCells || '-'}` +
      ` firstRecall=${row.calleeNaturalFirstRecallCoverage || '-'}` +
      ` firstRecallStatus=${row.calleeNaturalFirstRecallStatus || '-'}` +
      ` naturalChoice=${row.calleeNaturalFirstRecallChoice || '-'}` +
      ` naturalChoiceStatus=${row.calleeNaturalFirstRecallChoiceStatus || '-'}` +
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
      ` abiPrimaryPlacement=${row.calleeAbiPrimaryEntryPlacementLowerBoundByCallee || '-'}` +
      ` abiPrimaryPlacementBasis=${row.calleeAbiPrimaryEntryPlacementLowerBoundBasis || '-'}` +
      ` abiPrimaryCost=${row.calleeAbiPrimaryEntryCostBreakdown || '-'}` +
      ` abiPrimaryPlacementCost=${row.calleeAbiPrimaryEntryPlacementCostBreakdown || '-'}` +
      ` abiPrimaryNet=${row.calleeAbiPrimaryEntryNet || '-'}` +
      ` abiPrimaryPlacementNet=${row.calleeAbiPrimaryEntryPlacementNet || '-'}` +
      ` abiPrimaryNeed=${row.calleeAbiPrimaryEntryNeed || '-'}` +
      ` abiPrimaryPlacementNeed=${row.calleeAbiPrimaryEntryPlacementNeed || '-'}` +
      ` abiPrimaryStatus=${row.calleeAbiPrimaryEntryStatus || '-'}` +
      ` abiPrimaryPlacementStatus=${row.calleeAbiPrimaryEntryPlacementStatus || '-'}` +
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
    ['argX2', row.callArgumentX2RestoreStatus],
    ['x2Class', row.callArgumentX2ClobberClassesByCallee],
    ['abiNearPrimaryNet', row.calleeAbiNearPositivePrimaryNet],
    ['abiPlacement', row.calleeAbiNearPositiveStackPlacementStatus],
    ['abiPlacementAction', row.calleeAbiNearPositiveStackPlacementRequiredAction],
    ['abiPureStack', row.calleeAbiPureStackPlacement],
    ['abiProofDisposition', row.calleeAbiPrimaryEntryProofDisposition],
    ['abiProtocol', row.calleeAbiPrimaryEntryProtocol],
    ['abiArgRestage', row.calleeAbiPrimaryEntryArgumentRestage],
    ['naturalChoice', row.calleeNaturalFirstRecallChoice],
    ['naturalChoiceStatus', row.calleeNaturalFirstRecallChoiceStatus],
    ['abiPrimarySiteModel', row.calleeAbiPrimaryEntryPlacementSiteModel],
    ['abiPrimarySiteStatus', row.calleeAbiPrimaryEntryPlacementSiteModelStatus],
    ['abiPrimaryPreCallPlacement', row.calleeAbiPrimaryEntryPreCallPlacement],
    ['abiPrimaryPreCallPlacementStatus', row.calleeAbiPrimaryEntryPreCallPlacementStatus],
    ['abiPrimaryPreCallPlacementAction', row.calleeAbiPrimaryEntryPreCallPlacementAction],
    ['abiPrimaryPreCallProof', row.calleeAbiPrimaryEntryPreCallPlacementProof],
    ['abiPrimaryPreCallProofStatus', row.calleeAbiPrimaryEntryPreCallPlacementProofStatus],
    ['abiPrimaryPreCallProofAction', row.calleeAbiPrimaryEntryPreCallPlacementProofAction],
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
