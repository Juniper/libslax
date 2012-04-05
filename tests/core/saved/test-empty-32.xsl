<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" xmlns:bit="http://xml.libslax.org/bit" xmlns:slax="http://xml.libslax.org/slax" version="1.0" extension-element-prefixes="bit slax">
  <xsl:import href="../import/junos.xsl"/>
  <xsl:variable name="slax-output" mvarname="output"/>
  <xsl:variable xmlns:slax="http://xml.libslax.org/slax" name="output" mutable="yes" select="slax:mvar-init(&quot;output&quot;, &quot;slax-output&quot;, $slax-output)" svarname="slax-output"/>
  <xsl:template match="/">
    <op-script-results>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:and(0011, 0101) -&gt; &quot;, bit:and(&quot;0011&quot;, &quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:and(0, 0) -&gt; &quot;, bit:and(0, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:and(0, 1) -&gt; &quot;, bit:and(0, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:and(1, 0) -&gt; &quot;, bit:and(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:and(1, 1) -&gt; &quot;, bit:and(1, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nand(0011, 0101) -&gt; &quot;, bit:nand(&quot;0011&quot;, &quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nand(0, 0) -&gt; &quot;, bit:nand(0, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nand(0, 1) -&gt; &quot;, bit:nand(0, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nand(1, 0) -&gt; &quot;, bit:nand(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nand(1, 1) -&gt; &quot;, bit:nand(1, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nor(0011, 0101) -&gt; &quot;, bit:nor(&quot;0011&quot;, &quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nor(0, 0) -&gt; &quot;, bit:nor(0, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nor(0, 1) -&gt; &quot;, bit:nor(0, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nor(1, 0) -&gt; &quot;, bit:nor(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:nor(1, 1) -&gt; &quot;, bit:nor(1, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(0011) -&gt; &quot;, bit:not(&quot;0011&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(0) -&gt; &quot;, bit:not(0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(1) -&gt; &quot;, bit:not(1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(01) -&gt; &quot;, bit:not(&quot;01&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(10) -&gt; &quot;, bit:not(&quot;10&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(0000) -&gt; &quot;, bit:not(&quot;0000&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:not(1111) -&gt; &quot;, bit:not(&quot;1111&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:or(0011, 0101) -&gt; &quot;, bit:or(&quot;0011&quot;, &quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:or(0, 0) -&gt; &quot;, bit:or(0, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:or(0, 1) -&gt; &quot;, bit:or(0, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:or(1, 0) -&gt; &quot;, bit:or(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:or(1, 1) -&gt; &quot;, bit:or(1, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xnor(0011, 0101) -&gt; &quot;, bit:xnor(&quot;0011&quot;, &quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xnor(0, 0) -&gt; &quot;, bit:xnor(0, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xnor(0, 1) -&gt; &quot;, bit:xnor(0, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xnor(1, 0) -&gt; &quot;, bit:xnor(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xnor(1, 1) -&gt; &quot;, bit:xnor(1, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xor(0011, 0101) -&gt; &quot;, bit:xor(&quot;0011&quot;, &quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xor(0, 0) -&gt; &quot;, bit:xor(0, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xor( 0, 1) -&gt; &quot;, bit:xor(0, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xor( 1, 0) -&gt; &quot;, bit:xor(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:xor( 1, 1) -&gt; &quot;, bit:xor(1, 1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:mask(8, 32) -&gt; &quot;, bit:mask(8, 32))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:mask(1, 0) -&gt; &quot;, bit:mask(1, 0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:mask(8) -&gt; &quot;, bit:mask(8))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:mask(4) -&gt; &quot;, bit:mask(4))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:mask(7) -&gt; &quot;, bit:mask(7))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-int(11111111) -&gt; &quot;, bit:to-int(&quot;11111111&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-int(00) -&gt; &quot;, bit:to-int(&quot;00&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-int(01) -&gt; &quot;, bit:to-int(&quot;01&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-int(10) -&gt; &quot;, bit:to-int(&quot;10&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-int(11) -&gt; &quot;, bit:to-int(&quot;11&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-int(0) -&gt; &quot;, bit:from-int(0))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-int(1) -&gt; &quot;, bit:from-int(1))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-int(2) -&gt; &quot;, bit:from-int(2))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-int(3) -&gt; &quot;, bit:from-int(3))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-int(255) -&gt; &quot;, bit:from-int(255))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-int(255, 16) -&gt; &quot;, bit:from-int(255, 16))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0000) -&gt; &quot;, bit:to-hex(&quot;0000&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0001) -&gt; &quot;, bit:to-hex(&quot;0001&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0010) -&gt; &quot;, bit:to-hex(&quot;0010&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0011) -&gt; &quot;, bit:to-hex(&quot;0011&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0100) -&gt; &quot;, bit:to-hex(&quot;0100&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0101) -&gt; &quot;, bit:to-hex(&quot;0101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0110) -&gt; &quot;, bit:to-hex(&quot;0110&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(0111) -&gt; &quot;, bit:to-hex(&quot;0111&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1000) -&gt; &quot;, bit:to-hex(&quot;1000&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1001) -&gt; &quot;, bit:to-hex(&quot;1001&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1010) -&gt; &quot;, bit:to-hex(&quot;1010&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1011) -&gt; &quot;, bit:to-hex(&quot;1011&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1100) -&gt; &quot;, bit:to-hex(&quot;1100&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1101) -&gt; &quot;, bit:to-hex(&quot;1101&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1110) -&gt; &quot;, bit:to-hex(&quot;1110&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:to-hex(1111) -&gt; &quot;, bit:to-hex(&quot;1111&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x0') -&gt; &quot;, bit:from-hex(&quot;0x0&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x1') -&gt; &quot;, bit:from-hex(&quot;0x1&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x2') -&gt; &quot;, bit:from-hex(&quot;0x2&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x3') -&gt; &quot;, bit:from-hex(&quot;0x3&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x4') -&gt; &quot;, bit:from-hex(&quot;0x4&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x5') -&gt; &quot;, bit:from-hex(&quot;0x5&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x6') -&gt; &quot;, bit:from-hex(&quot;0x6&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x7') -&gt; &quot;, bit:from-hex(&quot;0x7&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x8') -&gt; &quot;, bit:from-hex(&quot;0x8&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0x9') -&gt; &quot;, bit:from-hex(&quot;0x9&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0xA') -&gt; &quot;, bit:from-hex(&quot;0xA&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0xB') -&gt; &quot;, bit:from-hex(&quot;0xB&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0xC') -&gt; &quot;, bit:from-hex(&quot;0xC&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0xD') -&gt; &quot;, bit:from-hex(&quot;0xD&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0xE') -&gt; &quot;, bit:from-hex(&quot;0xE&quot;))"/>
      </xsl:message>
      <xsl:message>
        <xsl:value-of select="concat(&quot;bit:from-hex('0xF') -&gt; &quot;, bit:from-hex(&quot;0xF&quot;))"/>
      </xsl:message>
    </op-script-results>
  </xsl:template>
</xsl:stylesheet>
