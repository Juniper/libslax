<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="/">
    <xsl:variable name="inventory" select="inventory"/>
    <xsl:for-each select="$inventory/chassis/chassis-module/chassis-sub-module[part-number = &quot;750-000610&quot;]">
      <message>
        <xsl:value-of select="concat(&quot;Down rev PIC in &quot;, ../name, &quot;, &quot;, name, &quot;: &quot;, description)"/>
      </message>
    </xsl:for-each>
  </xsl:template>
</xsl:stylesheet>
