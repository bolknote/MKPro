import { registerIndex } from "./opcodes.ts";
import type {
  ExpressionAst,
  IndexedExpressionAst,
  ProgramAst,
  RegisterName,
  StateBankAst,
  StateBankMemberAst,
  StateBankElementAst,
} from "./types.ts";

export interface ResolvedStateBankMember {
  bank: StateBankAst;
  member: StateBankMemberAst;
}

export function bankMemberKey(base: string, field?: string): string {
  return field === undefined ? base : `${base}.${field}`;
}

export function bankSelectorVariableName(base: string, field?: string): string {
  return `__bank_selector_${field === undefined ? base : `${base}_${field}`}`;
}

export function findStateBankMember(
  ast: ProgramAst,
  expr: IndexedExpressionAst,
): ResolvedStateBankMember | undefined {
  const bank = ast.banks?.find((candidate) => candidate.name === expr.base);
  if (bank === undefined) return undefined;
  const member = bank.members.find((candidate) => candidate.name === expr.field);
  if (member === undefined) return undefined;
  return { bank, member };
}

export function stateBankElementNames(member: StateBankMemberAst): string[] {
  return member.elements.map((element) => element.name);
}

export function stateBankElementForIndex(
  member: StateBankMemberAst,
  index: number,
): StateBankElementAst | undefined {
  return member.elements.find((element) => element.index === index);
}

export function numericIndexValue(expr: ExpressionAst): number | undefined {
  if (expr.kind === "number") {
    const value = Number(expr.raw);
    return Number.isInteger(value) ? value : undefined;
  }
  if (expr.kind === "unary" && expr.op === "-") {
    const value = numericIndexValue(expr.expr);
    return value === undefined ? undefined : -value;
  }
  return undefined;
}

export function contiguousRegisterOffset(
  member: StateBankMemberAst,
  registers: Readonly<Record<string, RegisterName>>,
): number | undefined {
  let offset: number | undefined;
  for (const element of member.elements) {
    const register = registers[element.name];
    if (register === undefined) return undefined;
    const current = registerIndex(register) - element.index;
    if (offset === undefined) {
      offset = current;
      continue;
    }
    if (offset !== current) return undefined;
  }
  return offset;
}

