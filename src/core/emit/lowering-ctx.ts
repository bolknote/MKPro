import type { EmitContext } from "../compiler.ts";

/**
 * The lowering surface that the domain modules (lowering/*.ts) operate over.
 *
 * Ideally this would be a hand-written narrow interface, but the lowering graph
 * of EmitContext is fully interconnected (expression lowering dispatches into
 * display/spatial/coord/control-flow and back), so the realistic contract is
 * "the lowering surface of EmitContext". The thin orchestrator implements it by
 * construction; the alias keeps the domain functions decoupled from how that
 * surface is assembled and documents the dependency direction
 * (domains -> ctx, never the reverse via concrete fields).
 */
export type LoweringCtx = EmitContext;
