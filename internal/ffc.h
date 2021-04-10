#pragma once

#define __FFC_NULL 
#define __FFC_GET_MACRO1(_1, _2, N, ...) N
#define __FFC_GET_MACRO2(_1, _2, _3, _4, N, ...) N
#define __FFC_STRING(x) #x

#define __FFC_ATTRIBUTE(...) __attribute__((__VA_ARGS__))
#define __FFC_ATTR_SECTION(x) section(__FFC_STRING(x))
#define __FFC_RESOLVE_SECTION(x, y, z)	__FFC_ATTR_SECTION(.x.y.z)
#define __FFC_CREATE_SECTION(x, y, z)	__FFC_RESOLVE_SECTION(x, y, z)

#define __FFC_ASM_PUSH_SECTION(x)	pushsection .x
#define __FFC_ASM_POP_SECTION		popsection
#define __FFC_ASM_SECTION(x)		section .x

#ifndef __FFC_ARCH_NUM
	#error "Fatal FFC error: No architecture set"
#endif

#define __FFC_TARGET_COMBINE_IMPL(x, y)	x##y	
#define __FFC_TARGET_COMBINE(x, y)			__FFC_TARGET_COMBINE_IMPL(x, y)		
#define __FFC_TARGET_OVERLAY(x)				__FFC_TARGET_COMBINE(__FFC_TARGET_COMBINE(ov, __FFC_ARCH_NUM), __FFC_TARGET_COMBINE(_, x))
#define __FFC_TARGET_ARM				__FFC_TARGET_COMBINE(arm, __FFC_ARCH_NUM)

#define __FFC_ASM_RESOLVE_SECTION(x, y, z)		x.__FFC_TARGET_COMBINE(y., z)

#define __FFC_HOOK_ARM(address)				__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(hook, __FFC_TARGET_ARM, address))
#define __FFC_HOOK_OVERLAY(address, overlay)		__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(hook, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_LINK_ARM(address)				__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(rlnk, __FFC_TARGET_ARM, address))
#define __FFC_LINK_OVERLAY(address, overlay)		__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(rlnk, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_SAFE_ARM(address)				__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(safe, __FFC_TARGET_ARM, address))
#define __FFC_SAFE_OVERLAY(address, overlay)		__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(safe, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_RPLC_ARM(address)				__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(over, __FFC_TARGET_ARM, address))
#define __FFC_RPLC_OVERLAY(address, overlay)		__FFC_ATTRIBUTE(used, __FFC_CREATE_SECTION(over, __FFC_TARGET_OVERLAY(overlay), address))
//#define __FFC_BLOB(symbol, path)			asm(".global " #symbol "\n.type " #symbol ", %object\n.align 2\n" #symbol ":\n.incbin \"" path "\"\n.equ " #symbol "_size,.-" #symbol "\n.align 2");
//#define __FFC_BLOB_PART(symbol, path, skip, count)	asm("");

#define __FFC_ASM_HOOK_ARM(address)			__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(hook, __FFC_TARGET_ARM, address))
#define __FFC_ASM_HOOK_OVERLAY(address, overlay)	__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(hook, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_ASM_LINK_ARM(address)			__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(rlnk, __FFC_TARGET_ARM, address))
#define __FFC_ASM_LINK_OVERLAY(address, overlay)	__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(rlnk, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_ASM_SAFE_ARM(address)			__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(safe, __FFC_TARGET_ARM, address))
#define __FFC_ASM_SAFE_OVERLAY(address, overlay)	__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(safe, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_ASM_RPLC_ARM(address)			__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(over, __FFC_TARGET_ARM, address))
#define __FFC_ASM_RPLC_OVERLAY(address, overlay)	__FFC_ASM_SECTION(__FFC_ASM_RESOLVE_SECTION(over, __FFC_TARGET_OVERLAY(overlay), address))
#define __FFC_ASM_REVERT				__FFC_ASM_SECTION(text)

#if defined __FFC_LANG_C || defined __FFC_LANG_CPP

	#define hook(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_HOOK_OVERLAY, __FFC_HOOK_ARM)(__VA_ARGS__)
	#define rlnk(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_LINK_OVERLAY, __FFC_LINK_ARM)(__VA_ARGS__)
	#define safe(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_SAFE_OVERLAY, __FFC_SAFE_ARM)(__VA_ARGS__)
	#define over(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_RPLC_OVERLAY, __FFC_RPLC_ARM)(__VA_ARGS__)
	//#define blob(...)		__FFC_GET_MACRO2(__VA_ARGS__, __FFC_BLOB_PART, __FFC_NULL, __FFC_BLOB)(__VA_ARGS__)
	#define asm_func		__FFC_ATTRIBUTE(naked)
	#define nodisc			__FFC_ATTRIBUTE(used)
	#define thumb			__FFC_ATTRIBUTE(target("thumb"))

#elif defined __FFC_LANG_ASM

	#define hook(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_ASM_HOOK_OVERLAY, __FFC_ASM_HOOK_ARM)(__VA_ARGS__)
	#define rlnk(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_ASM_LINK_OVERLAY, __FFC_ASM_LINK_ARM)(__VA_ARGS__)
	#define safe(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_ASM_SAFE_OVERLAY, __FFC_ASM_SAFE_ARM)(__VA_ARGS__)
	#define over(...)		__FFC_GET_MACRO1(__VA_ARGS__, __FFC_ASM_RPLC_OVERLAY, __FFC_ASM_RPLC_ARM)(__VA_ARGS__)
	#define revert			__FFC_ASM_REVERT
	
#else
	#error "Fatal FFC error: No language target set"
#endif
