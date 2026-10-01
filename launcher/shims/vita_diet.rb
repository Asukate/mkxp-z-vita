# Vita shims for Pokemon Infinite Fusion, release version (no logging).
#
# What it does (all order-independent prepends; game files untouched):
#  1. Skip the Game.initialize prewarm of battle animations
#     (pbLoadBattleAnimations lazy-loads on demand anyway).
#  2. Pin English (pbChooseLanguage -> 0): the tree has no english.dat
#     and harness input cannot dismiss the prompt; French unsupported.
#  3. English table-free messages: _INTL/getFromHash return the English
#     source key with no table, so the 30 MB messages.dat (-> ~130 MB
#     unpacked) is never loaded, eagerly or lazily (delayedLoad drops
#     its pending file).
#  4. Skip the 230-frame intro cinematic (512x384 each, all retained by
#     RPG::Cache = ~172 MB transient). Skippable in-game content.
$VITA_SKIP_BATTLE_ANIMS = true

module VitaBattleAnimsGate
  def pbLoadBattleAnimations
    return nil if $VITA_SKIP_BATTLE_ANIMS

    super
  end
end

class Object
  prepend VitaBattleAnimsGate
end

module VitaLanguageGate
  def pbChooseLanguage
    0
  end
end

class Object
  prepend VitaLanguageGate
end

module VitaMessagesGate
  def loadMessageFile(filename)
    nil
  end

  def delayedLoad
    @filename = nil
    nil
  end
end

module VitaIntroGate
  def playIntroCinematic
    nil
  end
end

# NOTE (no slash gate): System.data_directory is "ux0:/data" with no
# trailing slash, and every known use site is safe with exactly that
# value (explicit slashes or File.join) EXCEPT 013_UI_Load
# copyKeybindings, which is skipped below. An earlier revision
# appended "/" here; the resulting "ux0:/data/" shape stat-ed during
# SaveData load destabilized boot the same silent way the missing
# slash did, so the value passes through untouched.
# Belt and suspenders for the fatal stat: the stock copy mirrors PC
# controller defaults into the user dir. On Vita the file is unused
# (built-in bindings apply, proven at the title screen), so skip the
# copy entirely instead of merely repairing its path.
module VitaCopyKeybindingsGate
  def copyKeybindings
    nil
  end
end

# Module gates without const_added (absent on this Ruby): catch every
# method definition globally. `def` inside module M calls M.method_added
# (instance methods) or M.singleton_method_added (def self.x); defining
# both on class Module covers every module and class. Prepending above
# the just-defined method shadows it while super still reaches it, so
# install timing needs no sweep. Bulletproof by contract: any failure
# here would break the game file being defined.
class Module
  def method_added(mid)
    begin
      if self == Messages && (mid == :loadMessageFile || mid == :delayedLoad)
        Messages.prepend(VitaMessagesGate)
        VitaDietHeartbeat.mark("GATE Messages##{mid}")
      elsif self == Scene_Intro && mid == :playIntroCinematic
        Scene_Intro.prepend(VitaIntroGate)
        VitaDietHeartbeat.mark("GATE Scene_Intro##{mid}")
      elsif self == PokemonLoadScreen && mid == :copyKeybindings
        PokemonLoadScreen.prepend(VitaCopyKeybindingsGate)
        VitaDietHeartbeat.mark("GATE PokemonLoadScreen##{mid}")
      end
    rescue Exception
      nil
    end
  end

  def singleton_method_added(mid)
    begin
      if self == Messages && (mid == :loadMessageFile || mid == :delayedLoad)
        Messages.prepend(VitaMessagesGate)
        VitaDietHeartbeat.mark("GATE Messages##{mid}")
      end
    rescue Exception
      nil
    end
  end
end

# Kept as a backup path; harmless if the hook never fires.
def Object.const_added(name)
  if name == :Messages
    Messages.prepend(VitaMessagesGate)
    VitaDietHeartbeat.mark("GATE Messages(const_added)")
  elsif name == :Scene_Intro
    Scene_Intro.prepend(VitaIntroGate)
    VitaDietHeartbeat.mark("GATE Scene_Intro(const_added)")
  elsif name == :PokemonLoadScreen
    PokemonLoadScreen.prepend(VitaCopyKeybindingsGate)
    VitaDietHeartbeat.mark("GATE PokemonLoadScreen(const_added)")
  end
rescue Exception
  nil
end

# Primary install path (proven on-device in rev6-8): wrap Object#eval
# (NOT Kernel#eval — cref safety, see rev1 lesson) and sweep for gate
# targets after every file completes. Inline, no threads, no hooks.
module VitaDietSweep
  @done = {}

  class << self
    def once(key)
      return true if @done[key]

      @done[key] = true
      false
    end

    def try_wrap
      if Object.const_defined?(:Messages, false)
        if Messages.method_defined?(:loadMessageFile) ||
           Messages.private_method_defined?(:loadMessageFile)
          unless once(:messages_load)
            Messages.prepend(VitaMessagesGate)
            VitaDietHeartbeat.mark("GATE Messages(sweep)")
          end
        end
      end
      if Object.const_defined?(:Scene_Intro, false)
        if Scene_Intro.method_defined?(:playIntroCinematic) ||
           Scene_Intro.private_method_defined?(:playIntroCinematic)
          unless once(:intro)
            Scene_Intro.prepend(VitaIntroGate)
            VitaDietHeartbeat.mark("GATE Scene_Intro(sweep)")
          end
        end
      end
      if Object.const_defined?(:PokemonLoadScreen, false)
        if PokemonLoadScreen.method_defined?(:copyKeybindings) ||
           PokemonLoadScreen.private_method_defined?(:copyKeybindings)
          unless once(:copykb)
            PokemonLoadScreen.prepend(VitaCopyKeybindingsGate)
            VitaDietHeartbeat.mark("GATE PokemonLoadScreen(sweep)")
          end
        end
      end
    rescue Exception
      nil
    end
  end
end

class Object
  alias_method :vita_diet_orig_eval, :eval unless method_defined?(:vita_diet_orig_eval)

  def eval(*args, **kwargs, &blk)
    result = vita_diet_orig_eval(*args, **kwargs, &blk)
    VitaDietSweep.try_wrap
    result
  rescue Exception => e
    begin
      VitaDietSweep.try_wrap
    rescue Exception
      nil
    end
    raise e
  end

  private :eval
  private :vita_diet_orig_eval
end

# Minimal observability: one marker that the diet loaded, one line per
# gate installed, and a slow heartbeat while the process lives. This is
# the release build's only window into the boot (no tracer here).
module VitaDietHeartbeat
  LOG = "ux0:/data/compat-census-round2-20260902/CNSINF001-diet.log"

  class << self
    def mark(message)
      File.open(LOG, "a") do |f|
        f.puts("[DIET] #{message}")
        f.flush
      end
    rescue Exception
      nil
    end
  end
end

begin
  File.open(VitaDietHeartbeat::LOG, "w") { |f| f.puts("[DIET] LOADED") }
rescue Exception
  nil
end

# NOTE: no datadir install of any kind (see note above); the
# copyKeybindings skip below is the complete fix for the only known
# slash-less concatenation.

# NOTE: no heartbeat thread. Rev9 proved Thread.new hangs this Ruby fork
# at spawn (diag eboot + thread diet = same LOADED-only silence as the
# release runs; all threadless runs boot). Progress signal is the GATE
# marks below, written inline during script eval.
