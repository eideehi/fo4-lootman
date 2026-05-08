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
