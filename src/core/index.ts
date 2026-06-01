export { compileMKPro, CompileError, setReferenceMetricsResolver } from "./compiler.ts";
export {
  analyzeProgramStackResidency,
  analyzeSourceStackResidency,
  analyzeStackResidencyWindows,
  summarizeStackResidencyCandidates,
} from "./emit/stack-residency-analysis.ts";
export { parseProgram, parseExpression, ParseError } from "./parser.ts";
export {
  opcodeCatalog,
  opcodeByCode,
  opcodeByName,
  findOpcodeName,
  registerFromText,
  registerIndex,
  addressToOpcode,
  codeToAddress,
  formatAddress,
} from "./opcodes.ts";
export {
  formalAddressInfo,
  formalAddressOrdinal,
  formatFormalAddressOpcode,
  formatOfficialAddress,
  officialAddressToOpcode,
  parseFormalAddressOpcode,
} from "./formal-address.ts";
export { verifySuperDarkSuffixLayout } from "./super-dark-layout.ts";
export {
  formatAll,
  formatExplain,
  formatHex,
  formatJson,
  formatKeys,
  formatListing,
  formatProgramPatch,
  formatProgramTokens,
  formatSetupProgram,
  formatSetupBlock,
} from "./format.ts";
export { MK61_PROFILE, machineSupports } from "./machineProfile.ts";
export type * from "./types.ts";
