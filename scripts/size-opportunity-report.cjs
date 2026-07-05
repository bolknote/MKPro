#!/usr/bin/env node

const { execFileSync } = require('child_process');
const fs = require('fs');
const path = require('path');

function usage() {
  console.error(`Usage: node scripts/size-opportunity-report.cjs [options] [file-or-dir...]

Options:
  --compiler PATH   mkpro-native binary to run (default: native/build/mkpro-native)
  --json            Print the raw aggregate as JSON
  --limit N         Analyze at most N input files
  --help            Show this help

With no file-or-dir arguments, the script analyzes examples/ and
examples/pending-optimizer/.`);
}

function parseArgs(argv) {
  const options = {
    compiler: 'native/build/mkpro-native',
    json: false,
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
  const helperTraffic = [];
  for (const opportunity of report.opportunities ?? []) {
    if (opportunity.variant !== 'helper-register-traffic') {
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
      unprofitableInputs: detail(details, 'valueAwareUnprofitableStackInputNames'),
      suggestedResidentInputs: detail(details, 'valueAwareSuggestedResidentInputNames'),
      profitBreakdown: detail(details, 'valueAwareStackInputProfitBreakdown'),
      materializeCellsByName: detail(details, 'valueAwareStackInputMaterializeCellsByName'),
      directMaterialization: detail(details, 'valueAwareDirectStackInputMaterialization'),
      directMaterializationStatus: detail(
        details,
        'valueAwareDirectStackInputMaterializationStatus',
      ),
      bestInput: detail(details, 'valueAwareBestStackInputCandidate'),
      bestInputNet: detail(details, 'valueAwareBestStackInputNetCells'),
      bestInputAdditionalRecallCellsToProfit:
        detail(details, 'valueAwareBestStackInputAdditionalRecallCellsToProfit'),
      calleeStackSurvival: detail(details, 'valueAwareCallPreservationCalleeStackSurvival'),
      calleeNaturalPreservedSlots: detail(
        details,
        'valueAwareCalleeAbiNaturalPreservedSlotsByCallee',
      ),
      calleeRemainingPreserveDepth: detail(
        details,
        'valueAwareCalleeAbiRemainingPreserveDepthByCallee',
      ),
      calleeAbiStatus:
        detail(details, 'valueAwareCalleeAbiCostModelStatus') ||
        detail(details, 'valueAwareCallPreservationCalleeStatus'),
    });
  }
  const nextActions = (report.nextActions ?? []).map((action) => ({
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
  }));
  return {
    file,
    totalCells: report.totalCells,
    helperTraffic,
    nextActions,
  };
}

function aggregate(rows) {
  const nextActionGroups = new Map();
  const helperPlanGroups = new Map();
  const seenActionGroupFiles = new Set();
  const seenHelperGroupFiles = new Set();
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
    helperTrafficCount: rows.reduce((sum, row) => sum + row.helperTraffic.length, 0),
    positiveHelperTraffic: rows.flatMap((row) => row.helperTraffic)
      .filter((row) => row.savings > 0)
      .sort((left, right) => right.savings - left.savings || left.file.localeCompare(right.file)),
    stalledHelperTraffic: rows.flatMap((row) => row.helperTraffic)
      .filter((row) => row.savings <= 0)
      .sort((left, right) => right.savings - left.savings || left.file.localeCompare(right.file)),
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
      ` variant=${best.bestVariant ?? ''}`,
  );
}

function printHelper(row) {
  console.log(
    `- ${row.savings} ${row.file} :: ${row.helper}` +
      ` traffic=${row.registerTrafficCells}` +
      ` shape=${row.trafficShape || '-'}` +
      ` plan=${row.planStatus || '-'}` +
      ` required=${row.requiredAction || '-'}` +
      ` cost=${row.costModelAction || '-'}` +
      ` inputs=${row.suggestedResidentInputs || row.profitableInputs || row.unprofitableInputs || '-'}` +
      ` bestInput=${row.bestInput || '-'}` +
      ` bestNet=${row.bestInputNet || '-'}` +
      ` need=${row.bestInputAdditionalRecallCellsToProfit || '-'}` +
      ` profit=${row.profitBreakdown || '-'}` +
      ` materialize=${row.materializeCellsByName || '-'}` +
      ` directMat=${row.directMaterializationStatus || '-'}:${row.directMaterialization || '-'}` +
      ` survival=${row.calleeStackSurvival || '-'}` +
      ` natural=${row.calleeNaturalPreservedSlots || '-'}` +
      ` remaining=${row.calleeRemainingPreserveDepth || '-'}` +
      ` callee=${row.calleeAbiStatus || '-'}`,
  );
}

function printAction(row) {
  console.log(
    `- ${row.status} potential=${row.potentialSavings} best=${row.bestSavings}` +
      ` ${row.file} :: ${row.source}=${row.action}` +
      ` variant=${row.bestVariant || '-'}` +
      ` helper=${row.helper || '-'}`,
  );
}

function printText(report) {
  console.log(`Analyzed files: ${report.analyzedFiles}`);
  console.log(`Total cells: ${report.totalCells}`);
  console.log(`Helper register-traffic opportunities: ${report.helperTrafficCount}`);

  console.log('\nPositive next actions:');
  if (report.positiveNextActions.length === 0) {
    console.log('(none)');
  } else {
    report.positiveNextActions.slice(0, 20).forEach(printAction);
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
    } else {
      printText(report);
    }
  } catch (error) {
    console.error(`size-opportunity-report: ${error.message}`);
    process.exit(1);
  }
}

main();
