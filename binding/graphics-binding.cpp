/*
 ** graphics-binding.cpp
 **
 ** This file is part of mkxp.
 **
 ** Copyright (C) 2013 - 2021 Amaryllis Kulla <ancurio@mapleshrine.eu>
 **
 ** mkxp is free software: you can redistribute it and/or modify
 ** it under the terms of the GNU General Public License as published by
 ** the Free Software Foundation, either version 2 of the License, or
 ** (at your option) any later version.
 **
 ** mkxp is distributed in the hope that it will be useful,
 ** but WITHOUT ANY WARRANTY; without even the implied warranty of
 ** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 ** GNU General Public License for more details.
 **
 ** You should have received a copy of the GNU General Public License
 ** along with mkxp.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "config.h"
#include "graphics.h"
#include "sharedstate.h"
#include "binding-util.h"
#include "../src/vita_uid_auditor_api.h"
#include "binding-types.h"
#include "exception.h"
#ifdef __vita__
#include "vita_startup_timer.h"
#endif

#ifdef __vita__
RB_METHOD(graphicsStartupElapsedMs)
{
    RB_UNUSED_PARAM;
    return rb_float_new(static_cast<double>(vitaStartupTimerElapsedUs()) / 1000.0);
}

RB_METHOD(graphicsStartupMark)
{
    RB_UNUSED_PARAM;
    const char *phase = NULL;
    rb_get_args(argc, argv, "z", &phase RB_ARG_END);
    vitaStartupTimerMark(phase);
    return Qnil;
}
#endif

RB_METHOD(graphicsDelta) {
    RB_UNUSED_PARAM;
    GFX_LOCK;
    VALUE ret = rb_float_new(shState->graphics().getDelta());
    GFX_UNLOCK;
    return ret;
}

#ifdef MKXPZ_VITA_DIAGNOSTICS
#include <ruby/debug.h>

bool vitaDiagS42LTakeBindingTrace();
extern "C" void vitaDiagLog(const char *tag, const char *format, ...);
static inline void s42lBindingMark(const char *msg)
{
    vitaDiagLog("TEXTFLOW", "S42L %s", msg);
}

static VALUE s42mTracePoint = Qnil;
static VALUE s42mTargetThread = Qnil;
static VALUE s42nCurrentWindow = Qnil;
static VALUE s42nWindowClass = Qnil;
static bool s42mTraceActive = false;
static bool s42nInCallback = false;
static bool s42nRootsRegistered = false;
static bool s42oPostBasic = false;
static unsigned int s42oSeq = 0;
static const unsigned int s42pSeekBudget = 48;
static VALUE s42pTarget = Qnil;
static bool s42pTargetActive = false;
static unsigned int s42pSeq = 0;
static const unsigned int s42pTargetBudget = 64;
static unsigned int s42qMovingCallCount = 0;
static bool s42rStationaryConfirmed = false;
static bool s42rFocusActive = false;
static unsigned int s42rSeq = 0;
static const unsigned int s42rSeekBudget = 256;
static const unsigned int s42rFocusBudget = 8192;
static unsigned int s42uMapEventOrdinal = 1;
static bool s42uMapEventsDone = false;
static bool s42uCommonEventsDone = false;
static bool s42uUpdateEventsDone = false;
static bool s42vOuterFocusActive = false;
static unsigned int s42vSeq = 0;
static const unsigned int s42vBudget = 8192;
static bool s42wLifecycleActive = false;
static unsigned int s42wSeq = 0;
static const unsigned int s42wBudget = 32768;
static unsigned int s42wFadeCalls = 1;
static unsigned int s42wFadeReturns = 1;
static unsigned int s42wSceneUpdateCalls = 0;
static unsigned int s42wSceneUpdateReturns = 0;
static bool s42xLifecycleActive = false;
static unsigned int s42xFadeCalls = 1;
static unsigned int s42xFadeReturns = 1;
static unsigned int s42xGraphicsCalls = 0;
static unsigned int s42xGraphicsReturns = 0;
static unsigned int s42xSceneUpdateCalls = 0;
static unsigned int s42xSceneUpdateReturns = 0;
static bool s42xFadeinReturned = false;
static bool s42yAwaitSteadyFrame = false;
static bool s42ySteadyActive = false;
static unsigned int s42yFrameOrdinal = 0;
static unsigned int s42yUpdateDepth = 0;
static unsigned int s42yGraphicsCalls = 0;
static unsigned int s42yGraphicsReturns = 0;
static unsigned int s42yBaseStage = 0;
static unsigned int s42nIter = 0;
static unsigned int s42nUpdateDepth = 0;
static ID s42nIdUpdate = 0;
static ID s42nIdUpdateAllWindows = 0;
static ID s42nIdUpdateBasic = 0;
static ID s42nIdVarname = 0;
static ID s42nIdIvar = 0;
static ID s42nIdLocalVariableGet = 0;
static ID s42nCurrentVarId = 0;
static ID s42qIdMoving = 0;
static ID s42qIdUpdateMove = 0;
static ID s42qIdUpdateStop = 0;
static ID s42rIdNearScreen = 0;
static ID s42rIdSelfMovable = 0;
static ID s42rIdUpdateSelfMovement = 0;
static ID s42rIdMoveTypeRandom = 0;
static ID s42rIdMoveTypeTowardPlayer = 0;
static ID s42rIdMoveTypeCustom = 0;
static ID s42tIdUpdateEvents = 0;
static ID s42tIdEachValue = 0;
static ID s42tIdCheckEventTriggerAuto = 0;
static ID s42uIdEach = 0;
static ID s42uIdUpdateVehicles = 0;
static ID s42uIdUpdateParallax = 0;
static ID s42vIdUpdateForFade = 0;
static ID s42vIdUpdateTileset = 0;
static ID s42vIdUpdateTilemap = 0;
static ID s42vIdUpdateCharacters = 0;
static ID s42vIdUpdateShadow = 0;
static ID s42vIdUpdateWeather = 0;
static ID s42vIdUpdatePictures = 0;
static ID s42vIdUpdateTimer = 0;
static ID s42vIdUpdateViewports = 0;
static ID s42xIdFadeLoop = 0;
static ID s42xIdFadein = 0;
static ID s42xIdPostStart = 0;
static ID s42xIdPerformTransition = 0;
static ID s42xIdMain = 0;

static const char *s42nKind(rb_event_flag_t event)
{
    if (event & RUBY_EVENT_C_CALL) return "C";
    if (event & RUBY_EVENT_C_RETURN) return "C";
    return "RUBY";
}

static const char *s42oEventName(rb_event_flag_t event)
{
    if (event & RUBY_EVENT_LINE) return "LINE";
    if (event & RUBY_EVENT_CALL) return "CALL";
    if (event & RUBY_EVENT_RETURN) return "RETURN";
    if (event & RUBY_EVENT_B_RETURN) return "B_RETURN";
    if (event & RUBY_EVENT_C_CALL) return "C_CALL";
    if (event & RUBY_EVENT_C_RETURN) return "C_RETURN";
    if (event & RUBY_EVENT_RAISE) return "RAISE";
    return "OTHER";
}

static bool s42pIsGameCharacterBasePath(VALUE pathValue)
{
    static const char target[] = "029:Game_CharacterBase";
    if (NIL_P(pathValue) || RSTRING_LEN(pathValue) != (long)(sizeof(target) - 1))
        return false;

    const char *path = RSTRING_PTR(pathValue);
    for (unsigned int i = 0; i < sizeof(target) - 1; ++i)
        if (path[i] != target[i])
            return false;
    return true;
}

static bool s42uIsSection(VALUE pathValue, char a, char b, char c)
{
    if (NIL_P(pathValue) || RSTRING_LEN(pathValue) < 4)
        return false;
    const char *path = RSTRING_PTR(pathValue);
    return path[0] == a && path[1] == b && path[2] == c && path[3] == ':';
}

static bool s42vClassIs(const char *actual, const char *expected)
{
    if (!actual || !expected)
        return false;
    while (*actual && *expected) {
        if (*actual != *expected)
            return false;
        ++actual;
        ++expected;
    }
    return *actual == '\0' && *expected == '\0';
}

static bool s42vIsSpritesetSubUpdate(ID mid)
{
    return mid == s42vIdUpdateTileset ||
           mid == s42vIdUpdateTilemap ||
           mid == s42uIdUpdateParallax ||
           mid == s42vIdUpdateCharacters ||
           mid == s42vIdUpdateShadow ||
           mid == s42vIdUpdateWeather ||
           mid == s42vIdUpdatePictures ||
           mid == s42vIdUpdateTimer ||
           mid == s42vIdUpdateViewports;
}

static bool s42xIsLifecycleMethod(ID mid)
{
    return mid == s42xIdFadeLoop ||
           mid == s42xIdFadein ||
           mid == s42xIdPostStart ||
           mid == s42xIdPerformTransition ||
           mid == s42xIdMain;
}

static void s42mTraceCallback(VALUE tpval, void *)
{
    if (!s42mTraceActive || s42nInCallback ||
        rb_thread_current() != s42mTargetThread)
        return;

    s42nInCallback = true;

    rb_trace_arg_t *arg = rb_tracearg_from_tracepoint(tpval);
    const rb_event_flag_t event = rb_tracearg_event_flag(arg);
    VALUE lineValue = rb_tracearg_lineno(arg);
    VALUE methodValue = rb_tracearg_method_id(arg);
    const long line = NUM2LONG(lineValue);
    const ID mid = NIL_P(methodValue) ? 0 : SYM2ID(methodValue);

    if (s42pTargetActive) {
        VALUE pathValue = rb_tracearg_path(arg);
        VALUE self = rb_tracearg_self(arg);
        const char *path = NIL_P(pathValue) ? "<nil>" : RSTRING_PTR(pathValue);
        const int pathLen = NIL_P(pathValue) ? 5 : (int)RSTRING_LEN(pathValue);
        const char *selfClass = NIL_P(self) ? "NilClass" : rb_obj_classname(self);
        const bool isTargetBase =
            self == s42pTarget && s42pIsGameCharacterBasePath(pathValue);
        const bool section145 = s42uIsSection(pathValue, '1', '4', '5');
        const bool section027 = s42uIsSection(pathValue, '0', '2', '7');
        const bool section151 = s42uIsSection(pathValue, '1', '5', '1');
        const bool section092 = s42uIsSection(pathValue, '0', '9', '2');
        const bool section090 = s42uIsSection(pathValue, '0', '9', '0');
        ++s42pSeq;

        if (s42ySteadyActive) {
            const bool isSceneMap = s42vClassIs(selfClass, "Scene_Map");

            if (event & RUBY_EVENT_RAISE) {
                VALUE exc = rb_tracearg_raised_exception(arg);
                vitaDiagLog("TEXTFLOW",
                            "S42Y RAISE frame=%u depth=%u gfx=%u/%u stage=%u path=%.*s line=%ld mid=%s self=%s exc=%s",
                            s42yFrameOrdinal, s42yUpdateDepth,
                            s42yGraphicsCalls, s42yGraphicsReturns,
                            s42yBaseStage, pathLen, path, line,
                            mid ? rb_id2name(mid) : "-",
                            selfClass ? selfClass : "?",
                            NIL_P(exc) ? "<nil>" : rb_obj_classname(exc));
            }

            if (isSceneMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_CALL)) {
                if (s42yUpdateDepth == 0) {
                    ++s42yFrameOrdinal;
                    s42yBaseStage = 0;
                    vitaDiagLog("TEXTFLOW",
                                "S42Y FRAME_BEGIN frame=%u root_path=%.*s line=%ld gfx=%u/%u",
                                s42yFrameOrdinal, pathLen, path, line,
                                s42yGraphicsCalls, s42yGraphicsReturns);
                }

                ++s42yUpdateDepth;

                if (section092) {
                    vitaDiagLog("TEXTFLOW",
                                "S42Y BASE_ENTER frame=%u depth=%u path=%.*s line=%ld",
                                s42yFrameOrdinal, s42yUpdateDepth,
                                pathLen, path, line);
                }
            }

            if (isSceneMap && section092 && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_LINE) && line >= 60 && line <= 65) {
                const char *stage =
                    line == 60 ? "super" :
                    line == 61 ? "game_map" :
                    line == 62 ? "game_player" :
                    line == 63 ? "update_tag" :
                    line == 64 ? "spriteset" :
                                 "encounter";
                s42yBaseStage = (unsigned int)(line - 59);
                vitaDiagLog("TEXTFLOW",
                            "S42Y BASE_STAGE frame=%u stage=%s stage_id=%u line=%ld depth=%u gfx=%u/%u",
                            s42yFrameOrdinal, stage, s42yBaseStage, line,
                            s42yUpdateDepth,
                            s42yGraphicsCalls, s42yGraphicsReturns);
            }

            if (isSceneMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_RETURN)) {
                if (section092) {
                    vitaDiagLog("TEXTFLOW",
                                "S42Y BASE_RETURN frame=%u depth=%u stage=%u gfx=%u/%u path=%.*s line=%ld",
                                s42yFrameOrdinal, s42yUpdateDepth,
                                s42yBaseStage,
                                s42yGraphicsCalls, s42yGraphicsReturns,
                                pathLen, path, line);
                }

                if (s42yUpdateDepth > 0)
                    --s42yUpdateDepth;

                if (s42yUpdateDepth == 0) {
                    vitaDiagLog("TEXTFLOW",
                                "S42Y FRAME_RETURN frame=%u stage=%u gfx=%u/%u root_return_path=%.*s line=%ld",
                                s42yFrameOrdinal, s42yBaseStage,
                                s42yGraphicsCalls, s42yGraphicsReturns,
                                pathLen, path, line);
                }
            }

            s42nInCallback = false;
            return;
        }

        if (s42xLifecycleActive) {
            const bool isSceneMap = s42vClassIs(selfClass, "Scene_Map");

            if (event & RUBY_EVENT_RAISE) {
                VALUE exc = rb_tracearg_raised_exception(arg);
                vitaDiagLog("TEXTFLOW",
                            "S42X RAISE fade=%u/%u gfx=%u/%u path=%.*s line=%ld mid=%s self=%s exc=%s",
                            s42xFadeCalls, s42xFadeReturns,
                            s42xGraphicsCalls, s42xGraphicsReturns,
                            pathLen, path, line,
                            mid ? rb_id2name(mid) : "-",
                            selfClass ? selfClass : "?",
                            NIL_P(exc) ? "<nil>" : rb_obj_classname(exc));
            }

            if (isSceneMap && section092 && mid == s42vIdUpdateForFade &&
                (event & RUBY_EVENT_CALL)) {
                ++s42xFadeCalls;
                vitaDiagLog("TEXTFLOW",
                            "S42X FADE_UPDATE_BEGIN ordinal=%u gfx=%u/%u path=%.*s line=%ld",
                            s42xFadeCalls, s42xGraphicsCalls, s42xGraphicsReturns,
                            pathLen, path, line);
            }

            if (isSceneMap && section092 && mid == s42vIdUpdateForFade &&
                (event & RUBY_EVENT_RETURN)) {
                ++s42xFadeReturns;
                vitaDiagLog("TEXTFLOW",
                            "S42X FADE_UPDATE_RETURN ordinal=%u gfx=%u/%u path=%.*s line=%ld",
                            s42xFadeReturns, s42xGraphicsCalls, s42xGraphicsReturns,
                            pathLen, path, line);
            }

            if (isSceneMap && (section090 || section092) &&
                s42xIsLifecycleMethod(mid) &&
                (event & (RUBY_EVENT_CALL | RUBY_EVENT_RETURN))) {
                vitaDiagLog("TEXTFLOW",
                            "S42X LIFECYCLE ev=%s method=%s fade=%u/%u gfx=%u/%u path=%.*s line=%ld",
                            s42oEventName(event), mid ? rb_id2name(mid) : "-",
                            s42xFadeCalls, s42xFadeReturns,
                            s42xGraphicsCalls, s42xGraphicsReturns,
                            pathLen, path, line);
            }

            if (isSceneMap && section092 && mid == s42xIdFadein &&
                (event & RUBY_EVENT_RETURN)) {
                s42xFadeinReturned = true;
                s42yAwaitSteadyFrame = true;
                vitaDiagLog("TEXTFLOW",
                            "S42X FADEIN_RETURN fade=%u/%u gfx=%u/%u",
                            s42xFadeCalls, s42xFadeReturns,
                            s42xGraphicsCalls, s42xGraphicsReturns);
                ba_snapshot("M12_fadein_return");
            }

            if (s42xFadeinReturned && s42yAwaitSteadyFrame &&
                isSceneMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_CALL)) {
                s42xLifecycleActive = false;
                s42yAwaitSteadyFrame = false;
                s42ySteadyActive = true;
                s42yFrameOrdinal = 1;
                s42yUpdateDepth = 1;
                s42yGraphicsCalls = 0;
                s42yGraphicsReturns = 0;
                s42yBaseStage = 0;
                vitaDiagLog("TEXTFLOW",
                            "S42Y STEADY_BEGIN frame=1 root_path=%.*s line=%ld prior_x_gfx=%u/%u",
                            pathLen, path, line,
                            s42xGraphicsCalls, s42xGraphicsReturns);
                vitaDiagLog("TEXTFLOW",
                            "S42Y FRAME_BEGIN frame=1 root_path=%.*s line=%ld gfx=0/0",
                            pathLen, path, line);
                if (section092) {
                    vitaDiagLog("TEXTFLOW",
                                "S42Y BASE_ENTER frame=1 depth=1 path=%.*s line=%ld",
                                pathLen, path, line);
                }
                s42nInCallback = false;
                return;
            }

            if (isSceneMap && section092 && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_CALL)) {
                ++s42xSceneUpdateCalls;
                vitaDiagLog("TEXTFLOW",
                            "S42X SCENE_UPDATE_BEGIN ordinal=%u fadein_returned=%d gfx=%u/%u path=%.*s line=%ld",
                            s42xSceneUpdateCalls, s42xFadeinReturned ? 1 : 0,
                            s42xGraphicsCalls, s42xGraphicsReturns,
                            pathLen, path, line);
            }

            if (isSceneMap && section092 && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_RETURN)) {
                ++s42xSceneUpdateReturns;
                vitaDiagLog("TEXTFLOW",
                            "S42X SCENE_UPDATE_RETURN ordinal=%u fadein_returned=%d gfx=%u/%u path=%.*s line=%ld",
                            s42xSceneUpdateReturns, s42xFadeinReturned ? 1 : 0,
                            s42xGraphicsCalls, s42xGraphicsReturns,
                            pathLen, path, line);
            }

            s42nInCallback = false;
            return;
        }

        if (s42wLifecycleActive) {
            const bool observed =
                event & (RUBY_EVENT_LINE | RUBY_EVENT_CALL |
                         RUBY_EVENT_RETURN | RUBY_EVENT_B_RETURN |
                         RUBY_EVENT_RAISE);
            if (!observed) {
                s42nInCallback = false;
                return;
            }

            const unsigned int wseq = ++s42wSeq;
            const bool isSceneMap = s42vClassIs(selfClass, "Scene_Map");

            if (event & RUBY_EVENT_RAISE) {
                VALUE exc = rb_tracearg_raised_exception(arg);
                vitaDiagLog("TEXTFLOW",
                            "S42W RAISE ruby_events=%u path=%.*s line=%ld mid=%s self=%s exc=%s",
                            wseq, pathLen, path, line,
                            mid ? rb_id2name(mid) : "-",
                            selfClass ? selfClass : "?",
                            NIL_P(exc) ? "<nil>" : rb_obj_classname(exc));
            }

            if ((section090 || section092) &&
                (event & (RUBY_EVENT_LINE | RUBY_EVENT_CALL |
                          RUBY_EVENT_RETURN | RUBY_EVENT_B_RETURN))) {
                vitaDiagLog("TEXTFLOW",
                            "S42W SCENE_EVT seq=%u ev=%s path=%.*s line=%ld mid=%s self=%s value=0x%08lx",
                            wseq, s42oEventName(event), pathLen, path, line,
                            mid ? rb_id2name(mid) : "-",
                            selfClass ? selfClass : "?", (unsigned long)self);
            }

            if (isSceneMap && section092 && mid == s42vIdUpdateForFade &&
                (event & RUBY_EVENT_CALL)) {
                ++s42wFadeCalls;
                vitaDiagLog("TEXTFLOW",
                            "S42W FADE_UPDATE_BEGIN ordinal=%u ruby_events=%u path=%.*s line=%ld",
                            s42wFadeCalls, wseq, pathLen, path, line);
            }

            if (isSceneMap && section092 && mid == s42vIdUpdateForFade &&
                (event & RUBY_EVENT_RETURN)) {
                ++s42wFadeReturns;
                vitaDiagLog("TEXTFLOW",
                            "S42W FADE_UPDATE_RETURN ordinal=%u ruby_events=%u path=%.*s line=%ld",
                            s42wFadeReturns, wseq, pathLen, path, line);
            }

            if (isSceneMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_CALL)) {
                ++s42wSceneUpdateCalls;
                vitaDiagLog("TEXTFLOW",
                            "S42W SCENE_UPDATE_BEGIN ordinal=%u ruby_events=%u path=%.*s line=%ld",
                            s42wSceneUpdateCalls, wseq, pathLen, path, line);
            }

            if (isSceneMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_RETURN)) {
                ++s42wSceneUpdateReturns;
                vitaDiagLog("TEXTFLOW",
                            "S42W SCENE_UPDATE_RETURN ordinal=%u ruby_events=%u path=%.*s line=%ld",
                            s42wSceneUpdateReturns, wseq, pathLen, path, line);
            }

            if (wseq >= s42wBudget) {
                s42wLifecycleActive = false;
                s42pTargetActive = false;
                s42pTarget = Qnil;
                s42mTraceActive = false;
                rb_tracepoint_disable(tpval);
                vitaDiagLog("TEXTFLOW",
                            "S42W LIFECYCLE_BUDGET_DONE ruby_events=%u fade_calls=%u fade_returns=%u scene_update_calls=%u scene_update_returns=%u",
                            wseq, s42wFadeCalls, s42wFadeReturns,
                            s42wSceneUpdateCalls, s42wSceneUpdateReturns);
            }

            s42nInCallback = false;
            return;
        }

        if (s42vOuterFocusActive) {
            const unsigned int vseq = ++s42vSeq;
            const bool isSpritesetMap = s42vClassIs(selfClass, "Spriteset_Map");

            if (event & RUBY_EVENT_RAISE) {
                VALUE exc = rb_tracearg_raised_exception(arg);
                vitaDiagLog("TEXTFLOW",
                            "S42V RAISE callbacks=%u path=%.*s line=%ld mid=%s self=%s exc=%s",
                            vseq, pathLen, path, line,
                            mid ? rb_id2name(mid) : "-",
                            selfClass ? selfClass : "?",
                            NIL_P(exc) ? "<nil>" : rb_obj_classname(exc));
            }

            if (section151 && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_RETURN)) {
                vitaDiagLog("TEXTFLOW",
                            "S42V HULOGU_UPDATE_RETURN callbacks=%u path=%.*s line=%ld self=%s value=0x%08lx",
                            vseq, pathLen, path, line,
                            selfClass ? selfClass : "?", (unsigned long)self);
            }

            if (section092 && mid == s42vIdUpdateForFade &&
                (event & RUBY_EVENT_LINE) && line == 89) {
                vitaDiagLog("TEXTFLOW",
                            "S42V SPRITESET_CALL_SITE callbacks=%u path=%.*s line=%ld",
                            vseq, pathLen, path, line);
            }

            if (isSpritesetMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_CALL)) {
                vitaDiagLog("TEXTFLOW",
                            "S42V SPRITESET_UPDATE_BEGIN callbacks=%u path=%.*s line=%ld self=Spriteset_Map value=0x%08lx",
                            vseq, pathLen, path, line, (unsigned long)self);
            }

            if (isSpritesetMap && s42vIsSpritesetSubUpdate(mid) &&
                (event & RUBY_EVENT_CALL)) {
                vitaDiagLog("TEXTFLOW",
                            "S42V SPRITESET_SUB_BEGIN callbacks=%u method=%s path=%.*s line=%ld",
                            vseq, mid ? rb_id2name(mid) : "-",
                            pathLen, path, line);
            }

            if (isSpritesetMap && s42vIsSpritesetSubUpdate(mid) &&
                (event & RUBY_EVENT_RETURN)) {
                vitaDiagLog("TEXTFLOW",
                            "S42V SPRITESET_SUB_RETURN callbacks=%u method=%s path=%.*s line=%ld",
                            vseq, mid ? rb_id2name(mid) : "-",
                            pathLen, path, line);
            }

            if (isSpritesetMap && mid == s42nIdUpdate &&
                (event & RUBY_EVENT_RETURN)) {
                vitaDiagLog("TEXTFLOW",
                            "S42V SPRITESET_UPDATE_RETURN callbacks=%u path=%.*s line=%ld self=Spriteset_Map value=0x%08lx",
                            vseq, pathLen, path, line, (unsigned long)self);
            }

            if (section092 && mid == s42vIdUpdateForFade &&
                (event & RUBY_EVENT_RETURN)) {
                vitaDiagLog("TEXTFLOW",
                            "S42V UPDATE_FOR_FADE_RETURN callbacks=%u path=%.*s line=%ld self=%s value=0x%08lx",
                            vseq, pathLen, path, line,
                            selfClass ? selfClass : "?", (unsigned long)self);
                s42vOuterFocusActive = false;
                s42xLifecycleActive = true;
                s42xFadeCalls = 1;
                s42xFadeReturns = 1;
                s42xGraphicsCalls = 0;
                s42xGraphicsReturns = 0;
                s42xSceneUpdateCalls = 0;
                s42xSceneUpdateReturns = 0;
                s42xFadeinReturned = false;
                vitaDiagLog("TEXTFLOW",
                            "S42X LIFECYCLE_BEGIN first_fade_return=1");
            } else if (vseq >= s42vBudget) {
                s42vOuterFocusActive = false;
                s42pTargetActive = false;
                s42pTarget = Qnil;
                s42mTraceActive = false;
                rb_tracepoint_disable(tpval);
                vitaDiagLog("TEXTFLOW",
                            "S42V OUTER_BUDGET_DONE callbacks=%u",
                            vseq);
            }

            s42nInCallback = false;
            return;
        }

        if (!s42rFocusActive) {
            if ((event & RUBY_EVENT_CALL) && isTargetBase &&
                mid == s42qIdMoving) {
                ++s42qMovingCallCount;
                if (s42qMovingCallCount <= 2) {
                    vitaDiagLog("TEXTFLOW",
                                "S42U MOVING_CALL count=%u target_event=%u self=%s value=0x%08lx",
                                s42qMovingCallCount, s42pSeq,
                                selfClass ? selfClass : "?", (unsigned long)self);
                }
            }

            if ((event & RUBY_EVENT_RETURN) && isTargetBase &&
                mid == s42qIdMoving && s42qMovingCallCount == 2 &&
                !s42rStationaryConfirmed) {
                VALUE ret = rb_tracearg_return_value(arg);
                const bool moving = RTEST(ret);
                vitaDiagLog("TEXTFLOW",
                            "S42U MOVING_RETURN target_event=%u truth=%d value=0x%08lx",
                            s42pSeq, moving ? 1 : 0, (unsigned long)ret);

                if (!moving) {
                    s42rStationaryConfirmed = true;
                    vitaDiagLog("TEXTFLOW",
                                "S42U STATIONARY_CONFIRMED target_event=%u",
                                s42pSeq);
                } else {
                    vitaDiagLog("TEXTFLOW",
                                "S42U MOVING_DIVERGENCE target_event=%u self=%s value=0x%08lx",
                                s42pSeq, selfClass ? selfClass : "?",
                                (unsigned long)self);
                }
            }

            const bool baseUpdateReturn =
                (event & RUBY_EVENT_RETURN) && mid == s42nIdUpdate &&
                isTargetBase;

            if (baseUpdateReturn) {
                s42rFocusActive = true;
                s42rSeq = 0;
                s42uMapEventOrdinal = 1;
                s42uMapEventsDone = false;
                s42uCommonEventsDone = false;
                s42uUpdateEventsDone = false;
                vitaDiagLog("TEXTFLOW",
                            "S42U CENSUS_BEGIN target_event=%u first_ordinal=1 self=%s value=0x%08lx callback_budget=%u",
                            s42pSeq, selfClass ? selfClass : "?",
                            (unsigned long)self, s42rFocusBudget);
                s42nInCallback = false;
                return;
            }

            if (s42pSeq >= s42rSeekBudget) {
                s42pTargetActive = false;
                s42pTarget = Qnil;
                s42mTraceActive = false;
                rb_tracepoint_disable(tpval);
                vitaDiagLog("TEXTFLOW",
                            "S42U SEEK_BUDGET_DONE target_event=%u moving_calls=%u stationary=%d",
                            s42pSeq, s42qMovingCallCount,
                            s42rStationaryConfirmed ? 1 : 0);
            }

            s42nInCallback = false;
            return;
        }

        const unsigned int seq = ++s42rSeq;

        if ((event & RUBY_EVENT_RAISE)) {
            VALUE exc = rb_tracearg_raised_exception(arg);
            vitaDiagLog("TEXTFLOW",
                        "S42U RAISE callbacks=%u ordinal=%u path=%.*s line=%ld mid=%s self=%s exc=%s",
                        seq, s42uMapEventOrdinal, pathLen, path, line,
                        mid ? rb_id2name(mid) : "-",
                        selfClass ? selfClass : "?",
                        NIL_P(exc) ? "<nil>" : rb_obj_classname(exc));
        }

        if (!s42uMapEventsDone && section145 && mid == s42nIdUpdate &&
            (event & RUBY_EVENT_CALL) && line == 611) {
            ++s42uMapEventOrdinal;
            vitaDiagLog("TEXTFLOW",
                        "S42U MAP_EVENT_BEGIN ordinal=%u callbacks=%u self=%s value=0x%08lx",
                        s42uMapEventOrdinal, seq,
                        selfClass ? selfClass : "?", (unsigned long)self);
        }

        if (!s42uMapEventsDone && section145 && mid == s42nIdUpdate &&
            (event & RUBY_EVENT_RETURN) && line == 614) {
            vitaDiagLog("TEXTFLOW",
                        "S42U MAP_EVENT_RETURN ordinal=%u callbacks=%u self=%s value=0x%08lx",
                        s42uMapEventOrdinal, seq,
                        selfClass ? selfClass : "?", (unsigned long)self);
        }

        if (!s42uMapEventsDone && section027 &&
            (event & RUBY_EVENT_B_RETURN) && line == 601) {
            vitaDiagLog("TEXTFLOW",
                        "S42U MAP_EVENT_BLOCK_DONE ordinal=%u callbacks=%u",
                        s42uMapEventOrdinal, seq);
        }

        if (!s42uMapEventsDone && section027 && mid == s42tIdEachValue &&
            (event & RUBY_EVENT_C_RETURN) && line == 601) {
            s42uMapEventsDone = true;
            vitaDiagLog("TEXTFLOW",
                        "S42U MAP_EVENTS_DONE count=%u callbacks=%u",
                        s42uMapEventOrdinal, seq);
        }

        if (s42uMapEventsDone && !s42uCommonEventsDone && section027 &&
            mid == s42uIdEach && (event & RUBY_EVENT_C_CALL) && line == 602) {
            vitaDiagLog("TEXTFLOW",
                        "S42U COMMON_EVENTS_BEGIN callbacks=%u", seq);
        }

        if (s42uMapEventsDone && !s42uUpdateEventsDone && section027 &&
            line == 602 && mid == s42uIdEach && (event & RUBY_EVENT_C_RETURN)) {
            s42uCommonEventsDone = true;
            vitaDiagLog("TEXTFLOW",
                        "S42U COMMON_EVENTS_DONE callbacks=%u", seq);
        }

        if (s42uMapEventsDone && !s42uUpdateEventsDone &&
            mid == s42nIdUpdate && !section027 &&
            (event & RUBY_EVENT_CALL)) {
            vitaDiagLog("TEXTFLOW",
                        "S42U COMMON_UPDATE_BEGIN callbacks=%u path=%.*s line=%ld self=%s value=0x%08lx",
                        seq, pathLen, path, line,
                        selfClass ? selfClass : "?", (unsigned long)self);
        }

        if (s42uMapEventsDone && !s42uUpdateEventsDone &&
            mid == s42nIdUpdate && !section027 &&
            (event & RUBY_EVENT_RETURN)) {
            vitaDiagLog("TEXTFLOW",
                        "S42U COMMON_UPDATE_RETURN callbacks=%u path=%.*s line=%ld self=%s value=0x%08lx",
                        seq, pathLen, path, line,
                        selfClass ? selfClass : "?", (unsigned long)self);
        }

        if (section027 && mid == s42tIdUpdateEvents &&
            (event & RUBY_EVENT_RETURN)) {
            s42uUpdateEventsDone = true;
            vitaDiagLog("TEXTFLOW",
                        "S42U UPDATE_EVENTS_RETURN callbacks=%u map_events=%u common_done=%d",
                        seq, s42uMapEventOrdinal,
                        s42uCommonEventsDone ? 1 : 0);
        }

        if (s42uUpdateEventsDone && mid == s42uIdUpdateVehicles &&
            (event & RUBY_EVENT_CALL)) {
            vitaDiagLog("TEXTFLOW", "S42U UPDATE_VEHICLES_BEGIN callbacks=%u", seq);
        }
        if (s42uUpdateEventsDone && mid == s42uIdUpdateVehicles &&
            (event & RUBY_EVENT_RETURN)) {
            vitaDiagLog("TEXTFLOW", "S42U UPDATE_VEHICLES_RETURN callbacks=%u", seq);
        }
        if (s42uUpdateEventsDone && mid == s42uIdUpdateParallax &&
            (event & RUBY_EVENT_CALL)) {
            vitaDiagLog("TEXTFLOW", "S42U UPDATE_PARALLAX_BEGIN callbacks=%u", seq);
        }
        if (s42uUpdateEventsDone && mid == s42uIdUpdateParallax &&
            (event & RUBY_EVENT_RETURN)) {
            vitaDiagLog("TEXTFLOW", "S42U UPDATE_PARALLAX_RETURN callbacks=%u", seq);
        }

        if (s42uUpdateEventsDone && mid == s42nIdUpdate && !section027 &&
            (event & RUBY_EVENT_CALL)) {
            vitaDiagLog("TEXTFLOW",
                        "S42U POST_EVENTS_UPDATE_BEGIN callbacks=%u path=%.*s line=%ld self=%s value=0x%08lx",
                        seq, pathLen, path, line,
                        selfClass ? selfClass : "?", (unsigned long)self);
        }
        if (s42uUpdateEventsDone && mid == s42nIdUpdate && !section027 &&
            (event & RUBY_EVENT_RETURN)) {
            vitaDiagLog("TEXTFLOW",
                        "S42U POST_EVENTS_UPDATE_RETURN callbacks=%u path=%.*s line=%ld self=%s value=0x%08lx",
                        seq, pathLen, path, line,
                        selfClass ? selfClass : "?", (unsigned long)self);
        }

        const bool gameMapUpdateReturn =
            s42uUpdateEventsDone && section027 && mid == s42nIdUpdate &&
            (event & RUBY_EVENT_RETURN) && line == 565;

        if (gameMapUpdateReturn) {
            vitaDiagLog("TEXTFLOW",
                        "S42V GAME_MAP_BASE_RETURN callbacks=%u map_events=%u",
                        seq, s42uMapEventOrdinal);
            s42rFocusActive = false;
            s42vOuterFocusActive = true;
            s42vSeq = 0;
            vitaDiagLog("TEXTFLOW",
                        "S42V OUTER_FOCUS_BEGIN callback_budget=%u",
                        s42vBudget);
        } else if (seq >= s42rFocusBudget) {
            s42rFocusActive = false;
            s42pTargetActive = false;
            s42pTarget = Qnil;
            s42mTraceActive = false;
            rb_tracepoint_disable(tpval);
            vitaDiagLog("TEXTFLOW",
                        "S42U CENSUS_BUDGET_DONE callbacks=%u ordinal=%u map_done=%d common_done=%d update_events_done=%d",
                        seq, s42uMapEventOrdinal,
                        s42uMapEventsDone ? 1 : 0,
                        s42uCommonEventsDone ? 1 : 0,
                        s42uUpdateEventsDone ? 1 : 0);
        }

        s42nInCallback = false;
        return;
    }

    if (s42oPostBasic) {
        VALUE pathValue = rb_tracearg_path(arg);
        VALUE self = rb_tracearg_self(arg);
        const char *path = NIL_P(pathValue) ? "<nil>" : RSTRING_PTR(pathValue);
        const int pathLen = NIL_P(pathValue) ? 5 : (int)RSTRING_LEN(pathValue);
        const char *method = mid ? rb_id2name(mid) : "-";
        const char *selfClass = NIL_P(self) ? "NilClass" : rb_obj_classname(self);
        const unsigned int seq = ++s42oSeq;

        vitaDiagLog("TEXTFLOW",
                    "S42P SEEK seq=%u ev=%s path=%.*s line=%ld mid=%s self=%s value=0x%08lx",
                    seq, s42oEventName(event), pathLen, path, line,
                    method ? method : "?", selfClass ? selfClass : "?",
                    (unsigned long)self);

        if ((event & RUBY_EVENT_CALL) && mid == s42nIdUpdate &&
            s42pIsGameCharacterBasePath(pathValue)) {
            s42oPostBasic = false;
            s42pTargetActive = true;
            s42pTarget = self;
            s42pSeq = 0;
            vitaDiagLog("TEXTFLOW",
                        "S42P TARGET_BEGIN seek_seq=%u self=%s value=0x%08lx",
                        seq, selfClass ? selfClass : "?", (unsigned long)self);
        } else if (seq >= s42pSeekBudget) {
            s42oPostBasic = false;
            s42mTraceActive = false;
            rb_tracepoint_disable(tpval);
            vitaDiagLog("TEXTFLOW",
                        "S42P SEEK_BUDGET_DONE seq=%u", seq);
        }

        s42nInCallback = false;
        return;
    }

    if (event & RUBY_EVENT_RAISE) {
        VALUE pathValue = rb_tracearg_path(arg);
        VALUE exc = rb_tracearg_raised_exception(arg);
        const char *path = NIL_P(pathValue) ? "<nil>" : RSTRING_PTR(pathValue);
        const int pathLen = NIL_P(pathValue) ? 5 : (int)RSTRING_LEN(pathValue);
        vitaDiagLog("TEXTFLOW",
                    "S42N RAISE path=%.*s line=%ld mid=%s exc=%s",
                    pathLen, path, line,
                    mid ? rb_id2name(mid) : "-",
                    NIL_P(exc) ? "<nil>" : rb_obj_classname(exc));
    }

    if ((event & RUBY_EVENT_LINE) &&
        mid == s42nIdUpdateAllWindows && line == 95) {
        VALUE binding = rb_tracearg_binding(arg);
        VALUE varName = rb_funcall(binding, s42nIdLocalVariableGet, 1,
                                   ID2SYM(s42nIdVarname));
        VALUE ivar = rb_funcall(binding, s42nIdLocalVariableGet, 1,
                                ID2SYM(s42nIdIvar));
        const unsigned int iter = ++s42nIter;

        s42nCurrentVarId = SYMBOL_P(varName) ? SYM2ID(varName) : 0;
        const char *var = s42nCurrentVarId ? rb_id2name(s42nCurrentVarId) : "?";
        const char *klass = NIL_P(ivar) ? "NilClass" : rb_obj_classname(ivar);
        const bool isWindow =
            !NIL_P(ivar) && RTEST(rb_obj_is_kind_of(ivar, s42nWindowClass));

        s42nCurrentWindow = isWindow ? ivar : Qnil;
        s42nUpdateDepth = 0;

        vitaDiagLog("TEXTFLOW",
                    "S42N ITER idx=%u var=%s class=%s is_window=%d value=0x%08lx",
                    iter, var ? var : "?", klass ? klass : "?",
                    isWindow ? 1 : 0, (unsigned long)ivar);

        if (isWindow) {
            vitaDiagLog("TEXTFLOW",
                        "S42N WINDOW_UPDATE_BEFORE idx=%u var=%s class=%s value=0x%08lx",
                        iter, var ? var : "?", klass ? klass : "?",
                        (unsigned long)ivar);
        }
    }

    if (!NIL_P(s42nCurrentWindow) && mid == s42nIdUpdate) {
        VALUE self = rb_tracearg_self(arg);

        if (self == s42nCurrentWindow &&
            (event & (RUBY_EVENT_CALL | RUBY_EVENT_C_CALL))) {
            ++s42nUpdateDepth;
            vitaDiagLog("TEXTFLOW",
                        "S42N UPDATE_DISPATCH_BEGIN depth=%u kind=%s var=%s class=%s value=0x%08lx",
                        s42nUpdateDepth, s42nKind(event),
                        s42nCurrentVarId ? rb_id2name(s42nCurrentVarId) : "?",
                        rb_obj_classname(self), (unsigned long)self);
        }

        if (self == s42nCurrentWindow &&
            (event & (RUBY_EVENT_RETURN | RUBY_EVENT_C_RETURN))) {
            vitaDiagLog("TEXTFLOW",
                        "S42N UPDATE_DISPATCH_RETURN depth=%u kind=%s var=%s class=%s value=0x%08lx",
                        s42nUpdateDepth, s42nKind(event),
                        s42nCurrentVarId ? rb_id2name(s42nCurrentVarId) : "?",
                        rb_obj_classname(self), (unsigned long)self);
            if (s42nUpdateDepth > 0)
                --s42nUpdateDepth;
        }
    }

    if ((event & RUBY_EVENT_B_RETURN) &&
        mid == s42nIdUpdateAllWindows && line == 96) {
        if (!NIL_P(s42nCurrentWindow)) {
            vitaDiagLog("TEXTFLOW",
                        "S42N WINDOW_UPDATE_AFTER idx=%u var=%s class=%s depth=%u value=0x%08lx",
                        s42nIter,
                        s42nCurrentVarId ? rb_id2name(s42nCurrentVarId) : "?",
                        rb_obj_classname(s42nCurrentWindow),
                        s42nUpdateDepth, (unsigned long)s42nCurrentWindow);
        }
        s42nCurrentWindow = Qnil;
        s42nCurrentVarId = 0;
        s42nUpdateDepth = 0;
    }

    if ((event & RUBY_EVENT_RETURN) && mid == s42nIdUpdateAllWindows) {
        vitaDiagLog("TEXTFLOW", "S42N UPDATE_ALL_WINDOWS_RETURN iterations=%u",
                    s42nIter);
    }

    if ((event & RUBY_EVENT_RETURN) && mid == s42nIdUpdateBasic) {
        vitaDiagLog("TEXTFLOW", "S42N UPDATE_BASIC_RETURN iterations=%u",
                    s42nIter);
        s42oPostBasic = true;
        s42oSeq = 0;
        vitaDiagLog("TEXTFLOW", "S42P SEEK_BEGIN budget=%u",
                    s42pSeekBudget);
    }

    s42nInCallback = false;
}

static void s42mEnsureTracePoint()
{
    if (!NIL_P(s42mTracePoint))
        return;

    if (!s42nRootsRegistered) {
        rb_gc_register_address(&s42mTracePoint);
        rb_gc_register_address(&s42mTargetThread);
        rb_gc_register_address(&s42nCurrentWindow);
        rb_gc_register_address(&s42nWindowClass);
        rb_gc_register_address(&s42pTarget);
        s42nRootsRegistered = true;
    }

    s42nIdUpdate = rb_intern("update");
    s42nIdUpdateAllWindows = rb_intern("update_all_windows");
    s42nIdUpdateBasic = rb_intern("update_basic");
    s42nIdVarname = rb_intern("varname");
    s42nIdIvar = rb_intern("ivar");
    s42nIdLocalVariableGet = rb_intern("local_variable_get");
    s42qIdMoving = rb_intern("moving?");
    s42qIdUpdateMove = rb_intern("update_move");
    s42qIdUpdateStop = rb_intern("update_stop");
    s42rIdNearScreen = rb_intern("near_the_screen?");
    s42rIdSelfMovable = rb_intern("self_movable?");
    s42rIdUpdateSelfMovement = rb_intern("update_self_movement");
    s42rIdMoveTypeRandom = rb_intern("move_type_random");
    s42rIdMoveTypeTowardPlayer = rb_intern("move_type_toward_player");
    s42rIdMoveTypeCustom = rb_intern("move_type_custom");
    s42tIdUpdateEvents = rb_intern("update_events");
    s42tIdEachValue = rb_intern("each_value");
    s42tIdCheckEventTriggerAuto = rb_intern("check_event_trigger_auto");
    s42uIdEach = rb_intern("each");
    s42uIdUpdateVehicles = rb_intern("update_vehicles");
    s42uIdUpdateParallax = rb_intern("update_parallax");
    s42vIdUpdateForFade = rb_intern("update_for_fade");
    s42vIdUpdateTileset = rb_intern("update_tileset");
    s42vIdUpdateTilemap = rb_intern("update_tilemap");
    s42vIdUpdateCharacters = rb_intern("update_characters");
    s42vIdUpdateShadow = rb_intern("update_shadow");
    s42vIdUpdateWeather = rb_intern("update_weather");
    s42vIdUpdatePictures = rb_intern("update_pictures");
    s42vIdUpdateTimer = rb_intern("update_timer");
    s42vIdUpdateViewports = rb_intern("update_viewports");
    s42xIdFadeLoop = rb_intern("fade_loop");
    s42xIdFadein = rb_intern("fadein");
    s42xIdPostStart = rb_intern("post_start");
    s42xIdPerformTransition = rb_intern("perform_transition");
    s42xIdMain = rb_intern("main");
    s42nWindowClass = rb_const_get(rb_cObject, rb_intern("Window"));

    s42mTracePoint = rb_tracepoint_new(
        Qnil,
        RUBY_EVENT_LINE | RUBY_EVENT_CALL | RUBY_EVENT_RETURN |
        RUBY_EVENT_B_RETURN | RUBY_EVENT_RAISE |
        RUBY_EVENT_C_CALL | RUBY_EVENT_C_RETURN,
        s42mTraceCallback,
        0);
}

static void s42mArmRubyTrace()
{
    s42mEnsureTracePoint();
    s42nIter = 0;
    s42nUpdateDepth = 0;
    s42nCurrentWindow = Qnil;
    s42nCurrentVarId = 0;
    s42oPostBasic = false;
    s42oSeq = 0;
    s42pTarget = Qnil;
    s42pTargetActive = false;
    s42pSeq = 0;
    s42qMovingCallCount = 0;
    s42rStationaryConfirmed = false;
    s42rFocusActive = false;
    s42rSeq = 0;
    s42uMapEventOrdinal = 1;
    s42uMapEventsDone = false;
    s42uCommonEventsDone = false;
    s42uUpdateEventsDone = false;
    s42vOuterFocusActive = false;
    s42vSeq = 0;
    s42wLifecycleActive = false;
    s42wSeq = 0;
    s42wFadeCalls = 1;
    s42wFadeReturns = 1;
    s42wSceneUpdateCalls = 0;
    s42wSceneUpdateReturns = 0;
    s42xLifecycleActive = false;
    s42xFadeCalls = 1;
    s42xFadeReturns = 1;
    s42xGraphicsCalls = 0;
    s42xGraphicsReturns = 0;
    s42xSceneUpdateCalls = 0;
    s42xSceneUpdateReturns = 0;
    s42xFadeinReturned = false;
    s42yAwaitSteadyFrame = false;
    s42ySteadyActive = false;
    s42yFrameOrdinal = 0;
    s42yUpdateDepth = 0;
    s42yGraphicsCalls = 0;
    s42yGraphicsReturns = 0;
    s42yBaseStage = 0;
    s42mTargetThread = rb_thread_current();
    s42mTraceActive = true;
    vitaDiagLog("TEXTFLOW", "S42N TRACE_ENABLE_BEGIN");
    rb_tracepoint_enable(s42mTracePoint);
    vitaDiagLog("TEXTFLOW", "S42N TRACE_ENABLE_DONE");
}
#else
static inline bool vitaDiagS42LTakeBindingTrace() { return false; }
static inline void s42lBindingMark(const char *) {}
#endif

RB_METHOD_GUARD(graphicsUpdate)
{
    vitaGpuPressureSafePoint("graphics_update");
    RB_UNUSED_PARAM;
#if RAPI_MAJOR >= 2
#ifdef MKXPZ_VITA_DIAGNOSTICS
    s42mEnsureTracePoint();
    const bool s42xNativeTrace = s42xLifecycleActive;
    const bool s42yNativeTrace = s42ySteadyActive;
    unsigned int s42xNativeOrdinal = 0;
    unsigned int s42yNativeOrdinal = 0;
    if (s42xNativeTrace) {
        s42xNativeOrdinal = ++s42xGraphicsCalls;
        vitaDiagLog("TEXTFLOW",
                    "S42X GFX_UPDATE_BEGIN ordinal=%u fade=%u/%u scene_updates=%u/%u",
                    s42xNativeOrdinal, s42xFadeCalls, s42xFadeReturns,
                    s42xSceneUpdateCalls, s42xSceneUpdateReturns);
    }
    if (s42yNativeTrace) {
        s42yNativeOrdinal = ++s42yGraphicsCalls;
        vitaDiagLog("TEXTFLOW",
                    "S42Y GFX_UPDATE_BEGIN frame=%u ordinal=%u depth=%u stage=%u",
                    s42yFrameOrdinal, s42yNativeOrdinal,
                    s42yUpdateDepth, s42yBaseStage);
    }
#endif
    bool s42lTrace = false;
    drop_gvl_guard([](void *opaque) -> void* {
        bool *trace = static_cast<bool *>(opaque);
        GFX_GUARD_EXC(
            shState->graphics().update();
            *trace = vitaDiagS42LTakeBindingTrace();
            if (*trace) {
                s42lBindingMark("NO_GVL_UPDATE_RETURN");
                s42lBindingMark("GFX_UNLOCK_BEGIN");
            }
        );
        if (*trace) {
            s42lBindingMark("GFX_UNLOCK_DONE");
            s42lBindingMark("NO_GVL_CALLBACK_RETURN");
        }
        return 0;
    }, &s42lTrace, 0, 0);
#ifdef MKXPZ_VITA_DIAGNOSTICS
    if (s42xNativeTrace) {
        ++s42xGraphicsReturns;
        vitaDiagLog("TEXTFLOW",
                    "S42X GFX_UPDATE_RETURN ordinal=%u fade=%u/%u scene_updates=%u/%u",
                    s42xNativeOrdinal, s42xFadeCalls, s42xFadeReturns,
                    s42xSceneUpdateCalls, s42xSceneUpdateReturns);
    }
    if (s42yNativeTrace) {
        ++s42yGraphicsReturns;
        vitaDiagLog("TEXTFLOW",
                    "S42Y GFX_UPDATE_RETURN frame=%u ordinal=%u depth=%u stage=%u",
                    s42yFrameOrdinal, s42yNativeOrdinal,
                    s42yUpdateDepth, s42yBaseStage);
    }
#endif
    if (s42lTrace) {
        s42lBindingMark("GVL_REACQUIRED");
        s42lBindingMark("BINDING_RETURN_QNIL");
#ifdef MKXPZ_VITA_DIAGNOSTICS
        s42mArmRubyTrace();
#endif
    }
#else
    shState->graphics().update();
#endif
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(graphicsAverageFrameRate)
{
    RB_UNUSED_PARAM;
    GFX_LOCK;
    VALUE ret = rb_float_new(shState->graphics().averageFrameRate());
    GFX_UNLOCK;
    return ret;
}

RB_METHOD_GUARD(graphicsFreeze)
{
    RB_UNUSED_PARAM;
    
#if RAPI_MAJOR >= 2
    drop_gvl_guard([](void*) -> void* {
        GFX_GUARD_EXC( shState->graphics().freeze(); );
        return 0;
    }, 0, 0, 0);
#else
    shState->graphics().freeze();
#endif
    
    return Qnil;
}
RB_METHOD_GUARD_END

typedef struct {
    int duration;
    const char *filename;
    int vague;
} TransitionArgs;

RB_METHOD_GUARD(graphicsTransition)
{
    RB_UNUSED_PARAM;
    
    int duration = 8;
    const char *filename = "";
    int vague = 40;
    
    rb_get_args(argc, argv, "|izi", &duration, &filename, &vague RB_ARG_END);
    
    TransitionArgs args = {duration, filename, vague};
    
#if RAPI_MAJOR >= 2
    drop_gvl_guard([](void *args) -> void* {
        TransitionArgs &a = *((TransitionArgs*)args);
        GFX_GUARD_EXC( shState->graphics().transition(a.duration,
                                                      a.filename,
                                                      a.vague
                                                     ); );
        return 0;
    }, &args, 0, 0);
#else
    GFX_GUARD_EXC( shState->graphics().transition(duration, filename, vague); )
#endif
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(graphicsFrameReset)
{
    RB_UNUSED_PARAM;
    
    GFX_LOCK;
    shState->graphics().frameReset();
    GFX_UNLOCK;
    
    return Qnil;
}

#define DEF_GRA_PROP_I(PropName) \
RB_METHOD(graphics##Get##PropName) \
{ \
RB_UNUSED_PARAM; \
return rb_fix_new(shState->graphics().get##PropName()); \
} \
RB_METHOD(graphics##Set##PropName) \
{ \
RB_UNUSED_PARAM; \
int value; \
rb_get_args(argc, argv, "i", &value RB_ARG_END); \
GFX_LOCK; \
shState->graphics().set##PropName(value); \
GFX_UNLOCK; \
return rb_fix_new(value); \
}

#define DEF_GRA_PROP_B(PropName) \
RB_METHOD(graphics##Get##PropName) \
{ \
RB_UNUSED_PARAM; \
return rb_bool_new(shState->graphics().get##PropName()); \
} \
RB_METHOD(graphics##Set##PropName) \
{ \
RB_UNUSED_PARAM; \
bool value; \
rb_get_args(argc, argv, "b", &value RB_ARG_END); \
GFX_LOCK; \
shState->graphics().set##PropName(value); \
GFX_UNLOCK; \
return rb_bool_new(value); \
}

#define DEF_GRA_PROP_F(PropName) \
RB_METHOD(graphics##Get##PropName) \
{ \
RB_UNUSED_PARAM; \
return rb_float_new(shState->graphics().get##PropName()); \
} \
RB_METHOD(graphics##Set##PropName) \
{ \
RB_UNUSED_PARAM; \
double value; \
rb_get_args(argc, argv, "f", &value RB_ARG_END); \
GFX_LOCK; \
shState->graphics().set##PropName(value); \
GFX_UNLOCK; \
return rb_float_new(value); \
}

RB_METHOD(graphicsWidth)
{
    RB_UNUSED_PARAM;
    
    return rb_fix_new(shState->graphics().width());
}

RB_METHOD(graphicsHeight)
{
    RB_UNUSED_PARAM;
    
    return rb_fix_new(shState->graphics().height());
}

RB_METHOD(graphicsDisplayWidth)
{
    RB_UNUSED_PARAM;
    
    return rb_fix_new(shState->graphics().displayWidth());
}

RB_METHOD(graphicsDisplayHeight)
{
    RB_UNUSED_PARAM;
    
    return rb_fix_new(shState->graphics().displayHeight());
}

RB_METHOD_GUARD(graphicsWait)
{
    RB_UNUSED_PARAM;
    
    int duration;
    rb_get_args(argc, argv, "i", &duration RB_ARG_END);
#if RAPI_MAJOR >= 2
    drop_gvl_guard([](void* d) -> void* {
        GFX_GUARD_EXC( shState->graphics().wait(*(int*)d); );
        return 0;
    }, (int*)&duration, 0, 0);
#else
    shState->graphics().wait(duration);
#endif
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(graphicsFadeout)
{
    RB_UNUSED_PARAM;
    
    int duration;
    rb_get_args(argc, argv, "i", &duration RB_ARG_END);
    
#if RAPI_MAJOR >= 2
    drop_gvl_guard([](void* d) -> void* {
        GFX_GUARD_EXC( shState->graphics().fadeout(*(int*)d); );
        return 0;
    }, (int*)&duration, 0, 0);
#else
    shState->graphics().fadeout(duration);
#endif
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD_GUARD(graphicsFadein)
{
    RB_UNUSED_PARAM;
    
    int duration;
    rb_get_args(argc, argv, "i", &duration RB_ARG_END);
    
#if RAPI_MAJOR >= 2
    drop_gvl_guard([](void* d) -> void* {
        GFX_GUARD_EXC( shState->graphics().fadein(*(int*)d); );
        return 0;
    }, (int*)&duration, 0, 0);
#else
    shState->graphics().fadein(duration);
#endif
    
    return Qnil;
}
RB_METHOD_GUARD_END

void bitmapInitProps(Bitmap *b, VALUE self);

RB_METHOD_GUARD(graphicsSnapToBitmap)
{
    RB_UNUSED_PARAM;
    
    Bitmap *result = 0;
    
    GFX_GUARD_EXC( result = shState->graphics().snapToBitmap(); );
    
    VALUE obj = wrapObject(result, BitmapType);
    bitmapInitProps(result, obj);
    return obj;
}
RB_METHOD_GUARD_END

RB_METHOD(graphicsResizeScreen)
{
    RB_UNUSED_PARAM;
    
    int width, height;
    rb_get_args(argc, argv, "ii", &width, &height RB_ARG_END);
    
    GFX_LOCK;
    shState->graphics().resizeScreen(width, height);
    GFX_UNLOCK;
    
    return Qnil;
}

RB_METHOD(graphicsResizeWindow)
{
    RB_UNUSED_PARAM;
    
    int width, height;
    bool center = false;
    rb_get_args(argc, argv, "ii|b", &width, &height, &center RB_ARG_END);
    
    
    GFX_LOCK;
    shState->graphics().resizeWindow(width, height, center);
    GFX_UNLOCK;
    
    return Qnil;
}

RB_METHOD_GUARD(graphicsReset)
{
    RB_UNUSED_PARAM;
    
    GFX_GUARD_EXC( shState->graphics().reset(); );
    
    return Qnil;
}
RB_METHOD_GUARD_END

RB_METHOD(graphicsCenter)
{
    RB_UNUSED_PARAM;
    
    shState->graphics().center();
    return Qnil;
}

typedef struct {
    const char *filename;
    int volume;
    bool skippable;
} PlayMovieArgs;

void *playMovieInternal(void *args) {
    PlayMovieArgs *a = (PlayMovieArgs*)args;
    GFX_GUARD_EXC( shState->graphics().playMovie(a->filename, a->volume, a->skippable); );
    
    // Signals for shutdown or reset only make playMovie quit early,
    // so check again
    shState->checkShutdown();
    shState->checkReset();
    
    return 0;
}

RB_METHOD_GUARD(graphicsPlayMovie)
{
    RB_UNUSED_PARAM;
    
    VALUE filename, volumeArg, skippable;
    rb_scan_args(argc, argv, "12", &filename, &volumeArg, &skippable);
    SafeStringValue(filename);
    
    bool skip;
    rb_bool_arg(skippable, &skip);

    // TODO: Video control inputs (e.g. skip, pause)

    PlayMovieArgs args{};
    args.filename = RSTRING_PTR(filename);
    args.volume = (volumeArg == Qnil) ? 100 : NUM2INT(volumeArg);;
    args.skippable = skip;
#if RAPI_MAJOR >= 2
    drop_gvl_guard(playMovieInternal, &args, 0, 0);
#else
    playMovieInternal(&args);
#endif
    
    return Qnil;
}
RB_METHOD_GUARD_END

void graphicsScreenshotInternal(const char *filename)
{
    GFX_GUARD_EXC(shState->graphics().screenshot(filename););
}

RB_METHOD_GUARD(graphicsScreenshot)
{
    RB_UNUSED_PARAM;

    VALUE filename;
    rb_scan_args(argc, argv, "1", &filename);
    SafeStringValue(filename);
    
#if RAPI_MAJOR >= 2
    drop_gvl_guard([](void* fn) -> void* {
        graphicsScreenshotInternal((const char*)fn);
        return 0;
    }, (void*)RSTRING_PTR(filename), 0, 0);
#else
    graphicsScreenshotInternal(RSTRING_PTR(filename));
#endif
    return Qnil;
}
RB_METHOD_GUARD_END

DEF_GRA_PROP_I(FrameRate)
DEF_GRA_PROP_I(FrameCount)
DEF_GRA_PROP_I(Brightness)

DEF_GRA_PROP_B(Fullscreen)
DEF_GRA_PROP_B(ShowCursor)
DEF_GRA_PROP_F(Scale)
DEF_GRA_PROP_B(Frameskip)
DEF_GRA_PROP_B(FixedAspectRatio)
DEF_GRA_PROP_I(SmoothScaling)
DEF_GRA_PROP_B(IntegerScaling)
DEF_GRA_PROP_B(LastMileScaling)
DEF_GRA_PROP_B(Threadsafe)

#define INIT_GRA_PROP_BIND(PropName, prop_name_s) \
{ \
_rb_define_module_function(module, prop_name_s, graphics##Get##PropName); \
_rb_define_module_function(module, prop_name_s "=", graphics##Set##PropName); \
}

#ifdef MKXPZ_VITA_DIAGNOSTICS
#include "frame_profile.h"
#include <SDL_timer.h>
static bool profileGcActive = false;
static void profileGcEvent(rb_event_flag_t event, VALUE, VALUE, ID, VALUE) {
    // GC internal hooks must never allocate or invoke Ruby APIs.
    if (event == RUBY_INTERNAL_EVENT_GC_ENTER)
        profileGcActive = FrameProfile::enter(FrameProfile::GarbageCollection);
    else if (profileGcActive) {
        FrameProfile::leave(); profileGcActive = false;
    }
}
RB_METHOD(graphicsProfileLimits) {
    RB_UNUSED_PARAM;
    return rb_ary_new_from_args(2, UINT2NUM(FrameProfile::NativeCount),
                               UINT2NUM(FrameProfile::Categories));
}
RB_METHOD(graphicsProfileBegin) {
    RB_UNUSED_PARAM;
    static bool hooked = false;
    if (!hooked) {
        rb_add_event_hook(profileGcEvent, RUBY_INTERNAL_EVENT_GC_ENTER | RUBY_INTERNAL_EVENT_GC_EXIT, Qnil);
        hooked = true;
    }
    FrameProfile::begin(); return Qnil;
}
RB_METHOD(graphicsProfileEnter) {
    RB_UNUSED_PARAM;
    int id; rb_get_args(argc, argv, "i", &id RB_ARG_END);
    if (id < 0 || id >= int(FrameProfile::Categories)) rb_raise(rb_eArgError, "invalid profile category");
    return FrameProfile::enter(id) ? Qtrue : Qfalse;
}
RB_METHOD(graphicsProfileLeave) { RB_UNUSED_PARAM; FrameProfile::leave(); return Qnil; }
RB_METHOD(graphicsProfileEnd) {
    RB_UNUSED_PARAM;
    FrameProfile::end();
    auto &s = FrameProfile::state;
    VALUE rows = rb_ary_new();
    for (unsigned i = 0; i < s.size; ++i) {
        const auto &r = s.records[i];
        VALUE costs = rb_ary_new();
        for (unsigned j = 0; j < FrameProfile::Categories; ++j) {
            const auto &c = r.costs[j];
            if (c.ticks || c.calls || c.pixels)
                rb_ary_push(costs, rb_ary_new_from_args(4, UINT2NUM(j), ULL2NUM(c.ticks), ULL2NUM(c.calls), ULL2NUM(c.pixels)));
        }
        rb_ary_push(rows, rb_ary_new_from_args(3, ULL2NUM(r.ticks), UINT2NUM(r.kind), costs));
    }
    return rb_ary_new_from_args(4, ULL2NUM(SDL_GetPerformanceFrequency()), UINT2NUM(s.errors), UINT2NUM(s.dropped), rows);
}
RB_METHOD(graphicsProfileCalibrate) {
    RB_UNUSED_PARAM;
    const unsigned n = 10000;
    auto &s = FrameProfile::state;
    if (s.depth || s.capture) rb_raise(rb_eRuntimeError, "calibrate only before sampling");
    const uint64_t start = FrameProfile::clock();
    for (unsigned i=0; i<n; ++i) FrameProfile::clock();
    const uint64_t clocks = FrameProfile::clock()-start;
    FrameProfile::begin();
    const uint64_t startScopes = FrameProfile::clock();
    for (unsigned i=0; i<n; ++i) { FrameProfile::Scope scope(FrameProfile::Profiler); }
    const uint64_t scopes = FrameProfile::clock()-startScopes;
    s.capture = false;
    return rb_ary_new_from_args(4, UINT2NUM(n), ULL2NUM(SDL_GetPerformanceFrequency()), ULL2NUM(clocks), ULL2NUM(scopes));
}
#endif

void graphicsBindingInit()
{
    VALUE module = rb_define_module("Graphics");
#ifdef MKXPZ_VITA_DIAGNOSTICS
    _rb_define_module_function(module, "__profile_limits", graphicsProfileLimits);
    _rb_define_module_function(module, "__profile_begin", graphicsProfileBegin);
    _rb_define_module_function(module, "__profile_enter", graphicsProfileEnter);
    _rb_define_module_function(module, "__profile_leave", graphicsProfileLeave);
    _rb_define_module_function(module, "__profile_end", graphicsProfileEnd);
    _rb_define_module_function(module, "__profile_calibrate", graphicsProfileCalibrate);
#endif
    
    _rb_define_module_function(module, "delta", graphicsDelta);
    _rb_define_module_function(module, "update", graphicsUpdate);
    _rb_define_module_function(module, "freeze", graphicsFreeze);
    _rb_define_module_function(module, "transition", graphicsTransition);
    _rb_define_module_function(module, "frame_reset", graphicsFrameReset);
    _rb_define_module_function(module, "screenshot", graphicsScreenshot);
    
    _rb_define_module_function(module, "__reset__", graphicsReset);
    
    INIT_GRA_PROP_BIND( FrameRate,  "frame_rate"  );
    INIT_GRA_PROP_BIND( FrameCount, "frame_count" );
    _rb_define_module_function(module, "average_frame_rate", graphicsAverageFrameRate);
#ifdef __vita__
    _rb_define_module_function(module, "startup_elapsed_ms", graphicsStartupElapsedMs);
    _rb_define_module_function(module, "startup_mark", graphicsStartupMark);
#endif

    _rb_define_module_function(module, "width", graphicsWidth);
    _rb_define_module_function(module, "height", graphicsHeight);
    _rb_define_module_function(module, "display_width", graphicsDisplayWidth);
    _rb_define_module_function(module, "display_height", graphicsDisplayHeight);
    _rb_define_module_function(module, "wait", graphicsWait);
    _rb_define_module_function(module, "fadeout", graphicsFadeout);
    _rb_define_module_function(module, "fadein", graphicsFadein);
    _rb_define_module_function(module, "snap_to_bitmap", graphicsSnapToBitmap);
    _rb_define_module_function(module, "resize_screen", graphicsResizeScreen);
    _rb_define_module_function(module, "resize_window", graphicsResizeWindow);
    _rb_define_module_function(module, "center", graphicsCenter);
        
    INIT_GRA_PROP_BIND( Brightness, "brightness" );

    // end
    
    //if (rgssVer >= 3)
    //{
    _rb_define_module_function(module, "play_movie", graphicsPlayMovie);
    //}
    
    INIT_GRA_PROP_BIND( Fullscreen,       "fullscreen"         );
    INIT_GRA_PROP_BIND( ShowCursor,       "show_cursor"        );
    INIT_GRA_PROP_BIND( Scale,            "scale"              );
    INIT_GRA_PROP_BIND( Frameskip,        "frameskip"          );
    INIT_GRA_PROP_BIND( FixedAspectRatio, "fixed_aspect_ratio" );
    INIT_GRA_PROP_BIND( SmoothScaling,    "smooth_scaling"     );
    INIT_GRA_PROP_BIND( IntegerScaling,   "integer_scaling"    );
    INIT_GRA_PROP_BIND( LastMileScaling,  "last_mile_scaling"  );
    INIT_GRA_PROP_BIND( Threadsafe,       "thread_safe"        );
}
