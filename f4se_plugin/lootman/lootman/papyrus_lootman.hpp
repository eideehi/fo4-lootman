#pragma once

class VirtualMachine;

namespace papyrus_lootman
{
    bool Register(VirtualMachine* vm);
    void OnPreLoadGame();
}
