// Copyright (c) 2026 OpenArkStudio. Licensed under Apache-2.0. See LICENSE.
using System;
using Ark.Generated;

if (GuestPlayerFields.Id != 99u || GuestPlayerFields.Name != 100u ||
    GuestPlayerFields.Hp != 104u || GuestPlayerFields.Mp != 105u ||
    GuestPlayerFields.HpMax != 106u || GuestPlayerFields.MpMax != 107u ||
    GuestPlayerFields.ActionSeq != 108u)
{
    throw new InvalidOperationException("Generated C# field indices differ from the Entity schema.");
}
Console.WriteLine("Generated C# Entity field indices passed.");
