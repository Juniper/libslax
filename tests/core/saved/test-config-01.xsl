<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<xsl:stylesheet xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0">
  <xsl:template match="configuration">
    <error>
      <message>
        <xsl:value-of select="concat(&quot;System is named &quot;, system/host-name)"/>
      </message>
    </error>
  </xsl:template>
</xsl:stylesheet>
