# CommonLibF4 Hazards

This project carries local workarounds for known CommonLibF4 header/runtime
hazards. Apply these rules before touching native VM, Papyrus, form, or
attachment-mod code.

## Do Not Use `structure_wrapper`

Do not use:

```cpp
RE::BSScript::structure_wrapper<"ScriptName", "StructName">
```

Known failure: debug builds can trip a `BSFixedString` assertion because the
`structure_wrapper::name` string view is derived from a static buffer without a
trailing NUL.

Use VM APIs directly with a string-literal-backed `RE::BSFixedString`:

```cpp
RE::BSFixedString typeName("ScriptName#StructName");
RE::BSTSmartPointer<RE::BSScript::Struct> st;
vm->CreateStruct(typeName, st);
```

## Do Not Call `BGSMod::Attachment::Mod::GetData()`

Do not call:

```cpp
mod->GetData(containerData);
```

Known failure: the CommonLibF4 relocation ID for
`BGSMod::Attachment::Mod::GetData` is pinned to `REL::ID{ 0 }`, so dispatch can
jump to a garbage address and crash.

Read the property-mod block from the container buffer instead:

```cpp
const auto propModSpan = mod->GetBuffer<const BGSMod::Property::Mod>(
    static_cast<std::uint8_t>(BGSMod::Property::BLOCKIDS::kPMOD));
for (const auto& propMod : propModSpan) {
    // ...
}
```

Block id `1` (`BGSMod::Property::BLOCKIDS::kPMOD`) holds property mods. Block id
`0` (`kOMOD`) holds attachment instances.

## Do Not Use Default TESForm Pointer Packing

Returning `std::vector<TESObjectREFR*>`, `std::vector<TESForm*>`, or another
`std::vector<TESForm-derived*>` through the default `BindNativeMethod` packing
path can crash when the native function returns.

Cause: `RE::BSScript::PackVariable<object T>` calls the wrong virtual-machine
`CreateObject` overload because the overloaded virtual declarations do not
match the game binary vtable layout.

Required workaround:

- Specialize `RE::BSScript::detail::PackVariable<T>` for the exact pointer types
  returned by native functions.
- Build the `BSScript::Object` through
  `ObjectBindPolicy::bindInterface->CreateObjectWithProperties`.
- Bind the object with `binding.BindObject(object, handle)`.
- For generic `TESForm*`, use `form->GetFormType()` at runtime so each element
  marshals as its correct Papyrus subclass.

Keep the specializations in the same translation unit as the native function
definitions, before `BindNativeMethod(...)` instantiates `NativeFunction`.

## Do Not Hold a Lock Across an SEH-Guarded Untrusted Pointer Dereference

Do not do this inside an `ExecuteSehCallSafe`-guarded call path (or any other
`__try`/`__except` frame):

```cpp
std::lock_guard<std::mutex> guard(someLock);
DoSomethingWith(untrustedPointer->someField); // may fault while guard is live
```

Known failure: under MSVC's `/EHsc`, an SEH `__except` unwind does not run the
destructors of C++ objects in the intervening frames unwound to the handler,
including `std::lock_guard`/`std::unique_lock`. If code on an `ExecuteSehCallSafe`-
guarded path takes a lock and then, while still holding it, dereferences an
untrusted/engine-owned pointer or calls into vanilla game code that faults
(`EXCEPTION_ACCESS_VIOLATION` or `EXCEPTION_DATATYPE_MISALIGNMENT`),
`SehFilterRecoverable` lets the outer filter recover the call, but the lock's
destructor never ran. The lock is left permanently held, so every later
attempt to acquire it deadlocks the plugin. This is a single-crash-event
permanent lockup rather than a transient stall, but it presents to a player
the same way an ordinary performance freeze would.

Read the required field into a local value *before* constructing the
`lock_guard`/`unique_lock`, and release the lock before calling into
vanilla/engine code:

```cpp
// papyrus_lootman_hooks.cpp: GetRememberedLootManWorkshopForLocation
const TESFormID currentLocationId = currentLocation->formID;

TESFormID lootManWorkshopId = 0;
{
    std::lock_guard<std::mutex> guard(rememberedWorkshopSupplyLinkLock);
    // ... uses currentLocationId, not currentLocation, while the lock is held
}
```

When an untrusted read genuinely cannot be hoisted out of the lock's scope,
route it through its own SEH-guarded helper so a fault is caught inside that
helper's own `__try`/`__except` frame before it can unwind past the
lock_guard:

```cpp
// papyrus_lootman_diagnostics.cpp: GetInventoryStatusUnsafe
ReadLockGuard guard(inventoryList->rwLock);
std::uint32_t itemCount = 0;
if (TryGetInventoryItemCountSafe(inventoryList, itemCount)) // own __try/__except
{
    outEntries = itemCount;
}
```
