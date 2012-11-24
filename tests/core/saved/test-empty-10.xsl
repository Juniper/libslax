<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:output method="xml" indent="yes"/>
  <!-- See https://bugzilla.gnome.org/show_bug.cgi?id=629325 -->
  <xsl:template match="/">
    <op-script-results>
      <output>
        <xsl:value-of select="format-number(1.15, &quot;####.#&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(10.15, &quot;####.#&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(100.15, &quot;####.#&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(&quot;100.15&quot;, &quot;####.#&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(1000.15, &quot;####.#&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(&quot;100.15&quot;, &quot;####.################&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(100.15, &quot;####.################&quot;)"/>
      </output>
      <output>
        <xsl:value-of select="format-number(number(&quot;100.15&quot;), &quot;####.################&quot;)"/>
      </output>
    </op-script-results>
  </xsl:template>
</xsl:stylesheet>
