/* SPDX-License-Identifier: BSD-2-Clause-Patent
 *
 * SPDX-FileCopyrightText: 2026 the prplMesh contributors (see AUTHORS.md)
 *
 * This code is subject to the terms of the BSD+Patent license.
 * See LICENSE file for more details.
 */

#include "dpp/dpp_akm.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace {

using son::controller_dpp::dpp_akm_families;
using son::controller_dpp::dpp_akm_has_easymesh_decimal_alias;
using son::controller_dpp::dpp_akm_is_compatible;
using son::controller_dpp::dpp_akm_to_symbolic;
using son::controller_dpp::dpp_akm_token_family;

using Families = std::vector<std::string>;

TEST(DppAkmTokenFamily, SymbolicNamesAreCaseAndWhitespaceInsensitive)
{
    EXPECT_EQ(dpp_akm_token_family("psk"), "psk");
    EXPECT_EQ(dpp_akm_token_family("PSK"), "psk");
    EXPECT_EQ(dpp_akm_token_family(" sae "), "sae");
    EXPECT_EQ(dpp_akm_token_family("Dpp"), "dpp");
    EXPECT_EQ(dpp_akm_token_family("dot1x"), "dot1x");
}

// cppcheck-suppress syntaxError
TEST(DppAkmTokenFamily, SuiteSelectorsMapToFamilies)
{
    EXPECT_EQ(dpp_akm_token_family("506F9A02"), "dpp");
    EXPECT_EQ(dpp_akm_token_family("506f9a02"), "dpp");
    EXPECT_EQ(dpp_akm_token_family("000FAC02"), "psk");
    EXPECT_EQ(dpp_akm_token_family("000FAC04"), "psk");
    EXPECT_EQ(dpp_akm_token_family("000fac06"), "psk");
    EXPECT_EQ(dpp_akm_token_family("000FAC08"), "sae");
    EXPECT_EQ(dpp_akm_token_family("000FAC09"), "sae");
    EXPECT_EQ(dpp_akm_token_family("000FAC18"), "sae");
    EXPECT_EQ(dpp_akm_token_family("000FAC19"), "sae");
    EXPECT_EQ(dpp_akm_token_family("000FAC01"), "dot1x");
    EXPECT_EQ(dpp_akm_token_family("000FAC03"), "dot1x");
    EXPECT_EQ(dpp_akm_token_family("000FAC05"), "dot1x");
}

// Wi-Fi EasyMesh v6.1 Figure 9 bSTAList example: "506F9A02+000FAC08+000FAC24".
TEST(DppAkmTokenFamily, EasyMeshDecimalSaeExtKeySelectorMapsToSae)
{
    EXPECT_EQ(dpp_akm_token_family("000FAC24"), "sae");
}

TEST(DppAkmEasyMeshAlias, DetectsDecimalSaeExtKeySelector)
{
    EXPECT_TRUE(dpp_akm_has_easymesh_decimal_alias("506F9A02+000FAC08+000FAC24"));
    EXPECT_TRUE(dpp_akm_has_easymesh_decimal_alias(" 000fac24 "));
    EXPECT_FALSE(dpp_akm_has_easymesh_decimal_alias("506F9A02+000FAC18"));
    EXPECT_FALSE(dpp_akm_has_easymesh_decimal_alias("000FAC240"));
    EXPECT_FALSE(dpp_akm_has_easymesh_decimal_alias("psk+sae"));
    EXPECT_FALSE(dpp_akm_has_easymesh_decimal_alias(""));
}

TEST(DppAkmTokenFamily, UnknownTokensAreKeptLowerCase)
{
    EXPECT_EQ(dpp_akm_token_family("000FAC0B"), "000fac0b");
    EXPECT_EQ(dpp_akm_token_family("Open"), "open");
    EXPECT_EQ(dpp_akm_token_family("  "), "");
}

TEST(DppAkmFamilies, FamiliesAreSortedAndUnique)
{
    EXPECT_EQ(dpp_akm_families("506F9A02+000FAC08+000FAC24"), (Families{"dpp", "sae"}));
    EXPECT_EQ(dpp_akm_families("sae+psk"), (Families{"psk", "sae"}));
    EXPECT_EQ(dpp_akm_families("000FAC02+000FAC06+000FAC08"), (Families{"psk", "sae"}));
    EXPECT_TRUE(dpp_akm_families("").empty());
    EXPECT_TRUE(dpp_akm_families("++").empty());
}

// Field logs show "bSTAList akm=psk+sae"; this path must keep working.
TEST(DppAkmIsCompatible, SymbolicRequestStillMatchesSymbolicBss)
{
    EXPECT_TRUE(dpp_akm_is_compatible("psk+sae", "psk+sae"));
    EXPECT_TRUE(dpp_akm_is_compatible("psk+sae", "sae"));
    EXPECT_TRUE(dpp_akm_is_compatible("psk", "psk+sae"));
    EXPECT_TRUE(dpp_akm_is_compatible("PSK", "psk"));
}

// Version 2 Enrollees may send suite selectors (Easy Connect v3.0 4.3.5).
// These were rejected before because the tokens were compared literally.
TEST(DppAkmIsCompatible, SelectorRequestMatchesSymbolicBss)
{
    EXPECT_TRUE(dpp_akm_is_compatible("psk+sae", "000FAC02+000FAC08"));
    EXPECT_TRUE(dpp_akm_is_compatible("psk+sae", "506F9A02+000FAC08+000FAC24"));
    EXPECT_TRUE(dpp_akm_is_compatible("sae", "000FAC08"));
    EXPECT_TRUE(dpp_akm_is_compatible("psk", "000FAC06"));
}

TEST(DppAkmIsCompatible, DisjointAkmsAreIncompatible)
{
    EXPECT_FALSE(dpp_akm_is_compatible("psk", "000FAC08"));
    EXPECT_FALSE(dpp_akm_is_compatible("psk+sae", "506F9A02"));
    EXPECT_FALSE(dpp_akm_is_compatible("open", "psk+sae"));
    EXPECT_FALSE(dpp_akm_is_compatible("sae", "dot1x"));
}

TEST(DppAkmIsCompatible, MissingSideIsNotAConstraint)
{
    EXPECT_TRUE(dpp_akm_is_compatible("", "psk+sae"));
    EXPECT_TRUE(dpp_akm_is_compatible("psk", ""));
    EXPECT_TRUE(dpp_akm_is_compatible("", ""));
}

TEST(DppAkmIsCompatible, SeparatorOnlyRequestIsIncompatible)
{
    EXPECT_FALSE(dpp_akm_is_compatible("psk", "++"));
    EXPECT_FALSE(dpp_akm_is_compatible("psk+sae", "+++"));
    EXPECT_TRUE(dpp_akm_is_compatible("psk", "+psk++sae"));
}

// Same precedence as hostapd dpp_akm_from_str() for selector lists.
TEST(DppAkmToSymbolic, ToSymbolicFollowsHostapdPrecedence)
{
    EXPECT_EQ(dpp_akm_to_symbolic("000FAC02+000FAC08"), "psk+sae");
    EXPECT_EQ(dpp_akm_to_symbolic("506F9A02+000FAC08+000FAC24"), "dpp+sae");
    EXPECT_EQ(dpp_akm_to_symbolic("506F9A02+000FAC02+000FAC08"), "dpp+psk+sae");
    EXPECT_EQ(dpp_akm_to_symbolic("506F9A02+000FAC02"), "dpp");
    EXPECT_EQ(dpp_akm_to_symbolic("000FAC01+000FAC05"), "dot1x");
    EXPECT_EQ(dpp_akm_to_symbolic("SAE+PSK"), "psk+sae");
    EXPECT_EQ(dpp_akm_to_symbolic("psk+sae"), "psk+sae");
}

TEST(DppAkmToSymbolic, ToSymbolicIsEmptyWithoutKnownAkm)
{
    EXPECT_EQ(dpp_akm_to_symbolic("000FAC0B"), "");
    EXPECT_EQ(dpp_akm_to_symbolic("open"), "");
    EXPECT_EQ(dpp_akm_to_symbolic(""), "");
}

} // namespace
